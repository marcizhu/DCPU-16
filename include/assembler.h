#pragma once

#include <unordered_map>
#include <vector>
#include <string>

#include "dcpu16.h"

static const char ins_set[4*16*3+1] =
    "000JSR...............HCFINTIAGIASRFIIAQ........."
    "HWNHWQHWI......................................."
    "nbiSETADDSUBMULMLIDIVDVIMODMDIANDBORXORSHRASRSHL"
    "IFBIFCIFEIFNIFGIFAIFLIFU......ADXSBX......STISTD";

/* Disassemble() produces textual disassembly of single DCPU instruction at memory[pc] */
static std::unordered_multimap<uint32, std::string> SymbolLookup;

static const bool DisassemblyListing = false;

static const char regnames[][5] = {"A","B","C","X","Y","Z","I","J","PC","SP","EX","IA"};

static std::string Disassemble(unsigned pc, const uint16* memory)
{
    unsigned v = memory[pc++], o = (v & 0x1F), bb = (v>>5) & 0x1F, aa = (v>>10) & 0x3F;

    std::string opcode(&ins_set[3*(o ? 32+o : bb)], 3);
    auto doparam = [&](char which, unsigned v) -> std::string
    {
        std::string result(" ");
        char Buf[32], sep[4] = "\0+ ";
        if(v == 0x18) return result + (which=='b' ? "PUSH" : "POP");
        if(v >= 0x20) { std::sprintf(Buf, " 0x%X", uint16(int(v)-0x21)); return Buf; }
        if(reg_specs[v] & MEM) result += '[';
        if((reg_specs[v] & 0xFu)!=NOREG) { result += regnames[(reg_specs[v] & 0xFu)]; sep[0] = ' '; }
        if(reg_specs[v] & IMM) { sprintf(Buf, "%s0x%X", sep, memory[pc++]); result += Buf; }
        if(reg_specs[v] & IMM)
            for(auto i = SymbolLookup.equal_range(memory[pc-1]); i.first != i.second; ++i.first)
                result += "=" + i.first->second;
        if(reg_specs[v] & MEM) result += ']';
        return result;
    };
    for(auto i = SymbolLookup.equal_range(pc-1); i.first != i.second; ++i.first)
        opcode = i.first->second + ": " + opcode;

    std::string aparam = doparam('a', aa);
    if(o) { opcode += doparam('b', bb); opcode += ','; }
    return opcode += aparam;
}

class Assembler
{
    std::vector<uint16> memory;
    // Expression is a list of multiplicative terms. Each is a constant + list of summed symbols.
    typedef std::vector<std::pair<long, std::vector<std::pair<bool, std::string>>>> expression;
    // List of forward declarations, which can be patched later.
    std::vector<std::pair<uint16, expression>> forward_declarations;
    // pclist is used for side-by-side disassembly & source code listing.
    std::vector<std::pair<uint32, std::string>> pclist;
    // Remember known labels (name -> address).
    std::unordered_map<std::string, sint32> symbols;
    std::unordered_map<std::string, std::string> defines; // name->content
    // Macro: Macro name -> { macro content, list of parameter names }
    std::unordered_map<std::string, std::pair<std::string,std::vector<std::string>>> macros;
    // Macro call: Macro name, list of parameter values
    std::pair<std::string,std::vector<std::string>> macro_call;

    /* Define all reserved words */
    std::unordered_map<std::string, uint16> bops, operands;

    struct operand_type
    {
        bool set = false, brackets = false;
        uint16 addr = 0, shift = 0;
        expression terms;
        std::string string;

        void clear() { set=brackets=false; terms={{}}; } // Reset everything except addr&shift
    } op, op0;

    bool comment = false, label = false, sign = false, dat = false, string = false;
    std::string id, in_meta, recording_define, define_contents, recording_macro;
    unsigned fill_count = 1;
    uint16 pc=0;

    std::pair<uint16,int> simplify_expression(expression& expr, bool require_known = false)
    {
        // For each identifier in the term that is not a register, add it to the sum
        int register_number = -1;
        long const_total = 1;
        for(std::size_t b=0; b<expr.size(); )
        {
            auto& sum = expr[b];
            for(std::size_t a=0; a<sum.second.size(); )
            {
                const auto& v = sum.second[a];
                bool sign = v.first;
                // Was it the name of a CPU register?
                auto oi = operands.find(v.second);
                if(oi != operands.end())
                {
                    // Yes. Do some sanity checks
                    if(sign)
                        std::fprintf(stderr, "Error: Register operand '%s' cannot be negated\n", v.second.c_str());
                    if(expr.size() != 1)
                        std::fprintf(stderr, "Error: Register operand '%s' cannot appear in multiplications\n", v.second.c_str());
                    if(register_number != -1)
                        std::fprintf(stderr, "Error: Multiple registers used in same expression\n");
                    // Remember that the instruction refers to this register.
                    register_number = oi->second;
                }
                else
                {
                    // Not a CPU register. Is it a previously defined label?
                    auto si = symbols.find(v.second);
                    if(si == symbols.end())
                    {
                        // No, it's not known yet.
                        if(require_known)
                            std::fprintf(stderr, "Error: Unresolved forward declaration of '%s'\n", v.second.c_str());
                        // Keep it for later.
                        ++a;
                        continue;
                    }
                    // Yes. Treat it as an integer offset.
                    sum.first += (sign ? (-si->second) : (si->second));
                }
                sum.second.erase( sum.second.begin() + a);
            }
            if(!sum.second.empty())
                ++b;
            else
            {
                const_total *= sum.first;
                expr.erase( expr.begin() + b);
            }
        }
        // Add back the constant sum if it still will be needed.
        if(!expr.empty() && const_total != 1) expr.push_back( {const_total, {{}}} );
        if(!expr.empty()) const_total = 0;

        return {const_total,register_number};
    }

    void encode_operand(operand_type& op)
    {
        if(op.set && !op.string.empty())
        {
            if(in_meta == "INCLUDE")
            {
                in_meta.clear();
                op.set = false;
                std::string s; s.swap(op.string);
                FILE* fp = std::fopen(s.c_str(), "rt");
                if(!fp)
                    std::perror(s.c_str());
                else
                {
                    char Buf[131072];
                    int len = std::fread(Buf, 1, sizeof(Buf), fp);
                    std::fclose(fp);
                    parse_code( {Buf,Buf+len} );
                }
            }
            std::string s; s.swap(op.string);
            for(char c: s)
            {
                op.terms = {{}}; op.terms[0].first = (uint8)c;
                op.set   = true;
                encode_operand(op);
            }
        }
        else if(op.set)
        {
            // The assembler code contained a parameter at this point.
            // A parameter may be an identifier (from flush_id()),
            // or an integer constant.
            // Identifiers may also be names of CPU registers.

            // Calculate the identifiers.
            auto r = simplify_expression(op.terms);
            uint16 value          = r.first;
            int register_index = r.second;
            bool resolved      = op.terms.empty();
            if(!resolved) forward_declarations.emplace_back( pc, std::move(op.terms) );

            // Determine the type of operand to synthesize
            bool has_register    = register_index >= 0;
            bool has_brackets    = op.brackets;
            bool has_offset      = !resolved || value || !has_register;
            bool offset_is_small = resolved && uint16(value+1) <= 0x1F && op.shift == 10;

            // Sanity checking
            if((has_brackets | has_register) && (dat || !in_meta.empty()))
                std::fprintf(stderr, "Error: Cannot use brackets or registers in DAT, .ORG or .FILL\n");
            if(has_register && has_offset && !has_brackets && register_index != 0x1A)
                std::fprintf(stderr, "Error: Register + index without brackets is invalid.\n");
            if(has_register && has_offset && (register_index >= 8 && register_index != 0x1B && register_index != 0x1A))
                std::fprintf(stderr, "Error: Register + index are only valid with base registers and SP.\n");
            if(has_register && has_brackets && (register_index >= 8 && register_index != 0x1B))
                std::fprintf(stderr, "Error: Register references are only valid with base registers and SP.\n");

            if(in_meta == "ORG")  { pc         = value; in_meta.clear(); return; }
            if(in_meta == "FILL") { fill_count = value; in_meta.clear(); return; }
            if(!has_register && !has_brackets && has_offset && offset_is_small && !dat)
            {
                // It's a small integer that fits into the instruction verbatim.
                memory[op.addr] |= ((value + 0x21) & 0x3F) << op.shift;
            }
            else
            {
                // It contains a register, an integer, or both.
                unsigned code = has_brackets ? 0x1E : 0x1F;
                if(register_index == 0x1A && !has_offset) register_index = 0x19; // Encode PICK 0 as [SP]
                if(has_register) code = register_index;
                if(has_register && has_brackets) code |= (has_offset ? 0x10 : 0x08);
                if(register_index == 0x1B && has_brackets) code = has_offset ? 0x1A : 0x19; // PICK/PEEK

                if(!dat) memory[op.addr] |= code << op.shift; // Omit opcode for DAT
                if(has_offset) memory[pc++] = value;
            }
        }
    }
    void flush_operands()
    {
        for(; fill_count > 1; --fill_count) { auto b = op; encode_operand(b); }
        encode_operand(op);  op.clear();
        encode_operand(op0); op0.clear();
        sign = false;
    }
    std::string flush_id()
    {
        if(!id.empty())
        {
            // The assembler code contained an identifier at this point.
            // Determine what to do with it.

            // Was it a define-substitution?
            auto di = defines.find(id);
            if(di != defines.end())
            {
                // Yes. Parse that code.
                id.clear();
                return di->second;
            }
            // Does it begin a macro-substitution?
            auto mi = macros.find(id);
            if(mi != macros.end())
            {
                macro_call.first = id;
                id.clear();
                return {};
            }
            if(id == ".ENDMACRO")
            {
                id.clear();
                return {};
            }
            if(id == ".DAT") id = "DAT"; // cbm-basic uses .DAT for some reason
            // Was it a metacommand?
            if(id[0] == '.' || id[0] == '#')
            {
                in_meta = id.substr(1); id.clear();
                if(in_meta == "DEFINE" || in_meta == "ORG" || in_meta == "FILL"
                || in_meta == "MACRO" || in_meta == "INCLUDE") {}
                else std::fprintf(stderr, "Error: Unknown metacommand: %s\n", in_meta.c_str());
                return {};
            }
            // Did we just get an identifier for a ".define" command?
            if(in_meta == "DEFINE")
            {
                in_meta.clear();
                // Ok. Let's record a define!
                recording_define.swap(id);
                return {};
            }
            // Did we just get a macro name or a macro parameter name?
            if(in_meta == "MACRO")
            {
                if(recording_macro.empty())
                    recording_macro.swap(id); // Got a macro name
                else
                    macros[recording_macro].second.push_back(id); // Got a parameter name
                id.clear();
                // Ok, let's continue collecting macro data!
                return {};
            }
            // Was it a label? Add it as a known symbol.
            if(label)
            {
                if(!symbols.insert( {id,pc} ).second)
                    std::fprintf(stderr, "Error: Duplicate definition of '%s'\n", id.c_str());
                SymbolLookup.insert( {pc,id} );

                flush_operands();
                label = false;
            }
            else
            {
                // Was it an assembler mnemonic (i.e. name of an instruction)?
                auto ib = bops.find(id);
                if(ib != bops.end())
                {
                    // Yes. Place the opcode into the memory.
                    flush_operands();
                    if(DisassemblyListing) pclist.emplace_back( pc,"" );
                    auto b = ib->second; // This is an index of a string in ins_set[]
                    dat = b == 0;
                    op.addr  = pc;
                    op.shift = b < 32 ? 10 : 5;
                    if(!dat) memory[pc++] = b < 32 ? b*32 : b%32;
                }
                else
                {
                    // No. Remember the operand. flush_operand() will deal with it then.
                    op.terms.back().second.emplace_back( sign,id );
                    op.set = true;
                    sign   = false;
                }
            }
            id.clear();
        }
        return {};
    }

    std::string line;

    void parse_code(const std::string& code)
    {
        std::size_t p=0, a, b=code.size();
        for(a=0; a<b; )
        {
            char c = std::toupper(code[a]);
            if(DisassemblyListing) { line += code.substr(p,a+1-p - (c=='\n'||c=='\r')); p=a+1; }
            // Newline flushes the current line, and ends any comment
            if(c == '\n' || c == '\r')
            {
                if(!recording_macro.empty()) macros[recording_macro].first += c;
                if(!recording_define.empty())
                {
                    if(!defines.insert( { recording_define, define_contents } ).second)
                        std::fprintf(stderr, "Error: Duplicate define: %s\n", recording_define.c_str());
                    recording_define.clear();
                    define_contents.clear();
                }
                if(in_meta == "MACRO") in_meta.clear();
                parse_code(flush_id());
                flush_operands();
                if(DisassemblyListing && !pclist.empty()) { pclist.back().second += "||" + line; line.clear(); }
                comment=false; string=false; ++a; continue;
            }
            // The assembler will skip source code comments
            if(comment)  {                   ++a; continue; }
            if(c == ';') { comment=true;     ++a; continue; }
            // Are we recording a .define right now? If so, do no further parsing.
            if(!recording_define.empty())
            {
                // Recording will end when a newline is encountered.
                define_contents += code[a++];
                continue;
            }
            // Are we recording a .macro then?
            if(!recording_macro.empty() && in_meta.empty())
            {
                if(c == '.' && (code.compare(a+1,8,"ENDMACRO") || code.compare(a+1,8,"endmacro")))
                    recording_macro.clear();
                else
                    macros[recording_macro].first += code[a++];
                continue;
            }
            // String constant support
            if(c == '"') { string = !string; flush_operands(); ++a; continue; }
            if(string && c == '\\') ++a; // FIXME: Skips escapes now
            if(string)   { op.string += code[a]; op.set=true;  ++a; continue; }
            // Parse identifiers
            if((c >= 'A' && c <= 'Z')
            || c == '_'
            || (id.empty() && (c == '.' || c == '#'))
            || (!id.empty() && c >= '0' && c <= '9'))
                if(macro_call.first.empty()) { id += c;  ++a; continue; }
            // Anything else ends an identifier.
            parse_code(flush_id());
            // How about calling a macro?
            if(!macro_call.first.empty())
            {
                switch(c)
                {
                    case '(': case ',': // Add a parameter
                        macro_call.second.emplace_back(); break;
                    default: // Record macro parameter
                        if(!macro_call.second.empty())
                            macro_call.second.back() += code[a];
                        break;
                    case ')': // Invoke the macro
                        auto backup = defines;
                        auto m = macros.find(macro_call.first);
                        for(std::size_t n=0; n<macro_call.second.size() && n<m->second.second.size(); ++n)
                            defines.insert( {m->second.second[n], macro_call.second[n]} );
                        macro_call = {};
                        parse_code(m->second.first);
                        defines.swap(backup);
                }
                ++a; continue;
            }
            // A colon marks the current identifier as a label
            if(c == ':') { label=true;         ++a; continue; }
            // Spaces will be ignored, but they do end an identifier
            if(std::isspace(c)) { if(!in_meta.empty()) flush_operands(); ++a; continue; }
            // Some self-evident parsing of special characters
            if(c == ',') { if(dat) flush_operands(); op0 = op; op.clear(); op.shift = 10; ++a; continue; }
            if(c == '-') { sign = !sign;       ++a; continue; }
            if(c == '*') { op.terms.emplace_back(); sign = false; ++a; continue; }
            if(c == '+' || c == ']')         { ++a; continue; }
            if(c == '[') { op.brackets=true;   ++a; continue; }
            // Parentheses are ignored in general.
            if(c == '(' || c == ')') {         ++a; continue; }
            // Anything else: If it isn't a number, it's an error
            size_t endptr;
            try
            {
                long l = std::stoi(code.substr(a,30), &endptr, 0);
                a += endptr;
                // Was a number. Remember it as an operand.
                if(sign) l = -l;
                op.terms.back().first += l;
                op.set = true;
                sign   = false;
            }
            catch(...)
            {
                // Deal with invalid characters in input.
                std::fprintf(stderr, "Error: Invalid character: '%c'\n", c);
                ++a; continue;
            }
        }
    }
public:
    explicit Assembler(const std::string& file_contents) : memory(0x10000)
    {
        // Build the list of reserved words.
        //   Instructions:
        bops.insert( {"DAT",0} );
        for(unsigned a=0; a<sizeof(ins_set)/3; ++a)
            if(std::isupper(ins_set[a*3]))
                bops.insert( { {&ins_set[a*3], &ins_set[a*3]+3}, a } );
        //   Operands:
        operands.insert( { {"POP",0x18},{"PUSH",0x18},{"PEEK",0x19},{"PICK",0x1A},{"O",0x1D} } );
        for(unsigned a=0; a<0x20; ++a)
            if(reg_specs[a] < NOREG)
                operands[regnames[reg_specs[a]]] = a;

        parse_code(file_contents);

        // Flush the last line just in case it didn't have a newline at the end.
        parse_code(flush_id());
        flush_operands();
        std::fprintf(stderr, "Done assembling, PC=%X\n", pc);
        // Solve the forward references in the code (linking)
        for(auto&& r: forward_declarations)
            memory[r.first] += simplify_expression(r.second, true).first;

        if(DisassemblyListing)
        {
            pclist.emplace_back( pc,"" );

            for(auto pcp: pclist)
            {
                static decltype(pcp) prev;
                int len = std::fprintf(stderr, "%04X: ", prev.first);
                std::string opcode, s = Disassemble(prev.first, &memory[0]);;
                for(unsigned p=prev.first; p<pcp.first; opcode=s)
                    len += std::fprintf(stderr, " %04X", memory[p++]);
                std::fprintf(stderr, "%*s %16s %s\n",
                    40-len,"", opcode.c_str(), prev.second.c_str());
                prev.swap(pcp);
            }
        }
    }

    operator std::vector<uint16>() && { return std::move(memory); }
};