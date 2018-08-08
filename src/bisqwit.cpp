/* Creating a DCPU-16 (version 1.7) emulator! ��҂!!                     */
/* For all information, see http://dcpu.com/ �� ����҂�� Յ��ԄƖ��ֆׇ� */
#include <SDL.h>    /* Using libSDL for graphics & keyboard handling */
#include <csignal>

#include <cstdio>
#include <cstdint>
#include <cctype>
#include <cmath>

#include <string>
#include <vector>
#include <unordered_map>

typedef uint8_t  u8;  typedef int8_t  s8;
typedef uint16_t u16; typedef int16_t s16;
typedef uint32_t u32; typedef int32_t s32;

// Graphics settings and memory addresses.
static constexpr int
    HBORDER=24,VBORDER=3, WIDTH=128+HBORDER*2, HEIGHT=96+VBORDER*2, SCALE=4;

static const bool DisassemblyTrace   = false;
static const bool DisassemblyListing = false;

enum reg_index { A,B,C, X,Y,Z, I,J, PC,SP,EX, IA, noreg=0xF, mem=0x80, imm=0x40 };
static const char regnames[][5] = {"A","B","C","X","Y","Z","I","J","PC","SP","EX","IA"};
static const u8 reg_specs[0x20] =
    {A        ,B        ,C        ,X        ,Y        ,Z        ,I        ,J, // regs
     A    |mem,B    |mem,C    |mem,X    |mem,Y    |mem,Z    |mem,I    |mem,J    |mem,
     A|imm|mem,B|imm|mem,C|imm|mem,X|imm|mem,Y|imm|mem,Z|imm|mem,I|imm|mem,J|imm|mem,
     SP,       SP|mem, SP|imm|mem, SP, PC, EX, noreg|imm|mem, noreg|imm};

static const char ins_set[4*16*3+1] =
    "000JSR...............HCFINTIAGIASRFIIAQ........."
    "HWNHWQHWI......................................."
    "nbiSETADDSUBMULMLIDIVDVIMODMDIANDBORXORSHRASRSHL"
    "IFBIFCIFEIFNIFGIFAIFLIFU......ADXSBX......STISTD";

/* Disassemble() produces textual disassembly of single DCPU instruction at memory[pc] */
static std::unordered_multimap<u32, std::string> SymbolLookup;
// ^ This global array maps addresses into labels. Used by disassembler later.
static std::string Disassemble(unsigned pc, const u16* memory)
{
    unsigned v = memory[pc++], o = (v & 0x1F), bb = (v>>5) & 0x1F, aa = (v>>10) & 0x3F;

    std::string opcode(&ins_set[3*(o ? 32+o : bb)], 3);
    auto doparam = [&](char which, unsigned v) -> std::string
    {
        std::string result(" ");
        char Buf[32], sep[4] = "\0+ ";
        if(v == 0x18) return result + (which=='b' ? "PUSH" : "POP");
        if(v >= 0x20) { std::sprintf(Buf, " 0x%X", u16(int(v)-0x21)); return Buf; }
        if(reg_specs[v] & mem) result += '[';
        if((reg_specs[v] & 0xFu)!=noreg) { result += regnames[(reg_specs[v] & 0xFu)]; sep[0] = ' '; }
        if(reg_specs[v] & imm) { sprintf(Buf, "%s0x%X", sep, memory[pc++]); result += Buf; }
        if(reg_specs[v] & imm)
            for(auto i = SymbolLookup.equal_range(memory[pc-1]); i.first != i.second; ++i.first)
                result += "=" + i.first->second;
        if(reg_specs[v] & mem) result += ']';
        return result;
    };
    for(auto i = SymbolLookup.equal_range(pc-1); i.first != i.second; ++i.first)
        opcode = i.first->second + ": " + opcode;

    std::string aparam = doparam('a', aa);
    if(o) { opcode += doparam('b', bb); opcode += ','; }
    return opcode += aparam;
}

/* Assembler() reads an entire assembler file and translates it into a DCPU memory file */
class Assembler
{
    std::vector<u16> memory;
    // Expression is a list of multiplicative terms. Each is a constant + list of summed symbols.
    typedef std::vector<std::pair<long, std::vector<std::pair<bool, std::string>>>> expression;
    // List of forward declarations, which can be patched later.
    std::vector<std::pair<u16, expression>> forward_declarations;
    // pclist is used for side-by-side disassembly & source code listing.
    std::vector<std::pair<u32, std::string>> pclist;
    // Remember known labels (name -> address).
    std::unordered_map<std::string, s32> symbols;
    std::unordered_map<std::string, std::string> defines; // name->content
    // Macro: Macro name -> { macro content, list of parameter names }
    std::unordered_map<std::string, std::pair<std::string,std::vector<std::string>>> macros;
    // Macro call: Macro name, list of parameter values
    std::pair<std::string,std::vector<std::string>> macro_call;

    /* Define all reserved words */
    std::unordered_map<std::string, u16> bops, operands;

    struct operand_type
    {
        bool set = false, brackets = false;
        u16 addr = 0, shift = 0;
        expression terms;
        std::string string;

        void clear() { set=brackets=false; terms={{}}; } // Reset everything except addr&shift
    } op, op0;

    bool comment = false, label = false, sign = false, dat = false, string = false;
    std::string id, in_meta, recording_define, define_contents, recording_macro;
    unsigned fill_count = 1;
    u16 pc=0;

    std::pair<u16,int> simplify_expression(expression& expr, bool require_known = false)
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
                op.terms = {{}}; op.terms[0].first = (u8)c;
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
            u16 value          = r.first;
            int register_index = r.second;
            bool resolved      = op.terms.empty();
            if(!resolved) forward_declarations.emplace_back( pc, std::move(op.terms) );

            // Determine the type of operand to synthesize
            bool has_register    = register_index >= 0;
            bool has_brackets    = op.brackets;
            bool has_offset      = !resolved || value || !has_register;
            bool offset_is_small = resolved && u16(value+1) <= 0x1F && op.shift == 10;

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
            if(reg_specs[a] < noreg)
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

    operator std::vector<u16>() && { return std::move(memory); }
};

struct Emulator
{
    u16 memory[0x10000] = {}, reg[12] = {}, IRQqueue[256];
    u8 IRQhead = 0, IRQtail = 0;
    bool IRQqueuing = false;
    SDL_Surface* s = nullptr;

    Emulator()
    {
        // Initialize the SDL 1.2 library for graphics output.
        SDL_Init(SDL_INIT_VIDEO);
        SDL_InitSubSystem(SDL_INIT_VIDEO);
        std::signal(SIGINT, SIG_DFL);
        s = SDL_SetVideoMode(WIDTH*SCALE,HEIGHT*SCALE, 32,0);
        // For now, I am not using SDL 2.0, because it requires
        // considerably more lines of code to accomplish the
        // same things that I'm doing here.
        SDL_EnableUNICODE(1);
        SDL_EnableKeyRepeat(200,30);
    }

    class HardwareBase
    {
    protected:
        Emulator& emu;
        u32 ID, Version, Manufacturer, IRQ;

        HardwareBase(Emulator& e,u32 id,u32 v,u32 m)
            : emu(e),ID(id),Version(v),Manufacturer(m),IRQ(0) {}
    public:
        virtual ~HardwareBase() {}
        virtual void Interrupt() {}
        virtual void Tick() {}
        void Query()
        {
            emu.reg[A] = ID;
            emu.reg[B] = ID >> 16;
            emu.reg[C] = Version;
            emu.reg[X] = Manufacturer;
            emu.reg[Y] = Manufacturer >> 16;
        }
    };

    class HardwareClock: public HardwareBase // Generic Clock
    {
        unsigned Divider=0, Counter=0, Cycles=0;
    public:
        HardwareClock(Emulator& e) : HardwareBase(e,0x12d0b402,1,0) {}
        virtual void Interrupt()
        {
            switch(emu.reg[A])
            {
                case 0: Counter = Cycles = 0; Divider = 5000*emu.reg[B]; break;
                case 1: emu.reg[C] = Counter; break;
                case 2: IRQ = emu.reg[B]; break;
            }
        }
        virtual void Tick()
        {
            // The CPU ticks at 100 kHz; the Clock at 60 Hz. The ratio is 5000/3.
            for(Cycles += 3; Divider && Cycles >= Divider; Cycles -= Divider)
            {
                ++Counter;
                if(IRQ) emu.Interrupt(IRQ, true);
            }
        }
    } hw_clock{*this};

    class HardwareMonitor: public HardwareBase // Nya Elektriska Low Energy Monitor (LEM1802)
    {
        unsigned RAMbase=0, FontBase=0, PaletteBase=0, BorderColor=0, Cycles=0, Blink=0, Delay=60;
        bool cache_valid = false;
    public:
        HardwareMonitor(Emulator& e) : HardwareBase(e,0x7349f615,0x1802,0x1c6c8b36) {}
        virtual void Interrupt()
        {
            switch(emu.reg[A])
            {
                case 0:
                    if(emu.reg[B] && !RAMbase) Delay = 120;
                    RAMbase = emu.reg[B];
                    break;
                case 1: FontBase    = emu.reg[B];     break;
                case 2: PaletteBase = emu.reg[B];     cache_valid = false; break;
                case 3: BorderColor = emu.reg[B]&0xF; break;
                case 4: for(int n=0; n<256; ++n) { emu.memory[u16(emu.reg[B]+n)] = GetFontCell(n,true); emu.tick(); } break;
                case 5: for(int n=0; n< 16; ++n) { emu.memory[u16(emu.reg[B]+n)] = GetPalette(n, true); emu.tick(); } break;
            }
        }
        virtual void Tick()
        {
            // The screen refreshes at 60 Hz. The CPU ticks at 100 kHz. The ratio is 5000/3.
            for(Cycles += 3; Cycles >= 5000; Cycles -= 5000)
            {
                Render(++Blink & 32); // Toggle blink at 32 frame intervals
                if(Delay) --Delay;
            }
        }
    private:
        // For rendering, we will reuse most of the "text mode" code from DCPU 1.1.
        // Unfortunately, the graphics code with sprites will have to be deleted.

        u16 GetFontCell(unsigned n, bool force_builtin=false) const
        {
            // This is our built-in font.
            static const u16 font4x8[256] =
            { 0x0000,0x0000,0x3E65,0x653E,0x3E5B,0x5B3E,0x1E7C,0x1E00,0x1C7F,0x1C00,0x4C73,0x4C00,0x5C7F,0x5C00,0x183C,0x1800,
              0xE7C3,0xE7FF,0x1824,0x1800,0xE7DB,0xE7FF,0xE7DB,0xE7FF,0x2C72,0x2C00,0x607F,0x0507,0x607F,0x617F,0x2A1F,0x7C2A,
              0x7F3E,0x1C08,0x081C,0x3E7F,0x227F,0x7F22,0x5F00,0x5F00,0x0609,0x7F7F,0x9AA5,0xA559,0x6060,0x6060,0xA2FF,0xFFA2,
              0x027F,0x7F02,0x207F,0x7F20,0x1818,0x3C18,0x183C,0x1818,0x3020,0x2020,0x081C,0x1C08,0x707E,0x7E70,0x0E7E,0x7E0E,
              0x0000,0x0000,0x005F,0x0000,0x0700,0x0700,0x3E14,0x3E00,0x266B,0x3200,0x611C,0x4300,0x6659,0xE690,0x0005,0x0300,
              0x1C22,0x4100,0x4122,0x1C00,0x2A1C,0x2A00,0x083E,0x0800,0x00A0,0x6000,0x0808,0x0800,0x0060,0x0000,0x601C,0x0300,
              0x3E4D,0x3E00,0x427F,0x4000,0x6259,0x4600,0x2249,0x3600,0x0E08,0x7F00,0x2745,0x3900,0x3E49,0x3200,0x6119,0x0700,
              0x3649,0x3600,0x2649,0x3E00,0x0066,0x0000,0x8066,0x0000,0x0814,0x2241,0x1414,0x1400,0x4122,0x1408,0x0259,0x0600,
              0x3E59,0x5E00,0x7E09,0x7E00,0x7F49,0x3600,0x3E41,0x2200,0x7F41,0x3E00,0x7F49,0x4100,0x7F09,0x0100,0x3E49,0x3A00,
              0x7F08,0x7F00,0x417F,0x4100,0x2040,0x3F00,0x7F0C,0x7300,0x7F40,0x4000,0x7F0E,0x7F00,0x7E1C,0x7F00,0x7F41,0x7F00,
              0x7F09,0x0600,0x3E41,0xBE00,0x7F09,0x7600,0x2649,0x3200,0x017F,0x0100,0x7F40,0x7F00,0x1F60,0x1F00,0x7F30,0x7F00,
              0x771C,0x7700,0x0778,0x0700,0x615D,0x4300,0x007F,0x4100,0x0618,0x6000,0x0041,0x7F00,0x0C06,0x0C00,0x8080,0x8080,
              0x0003,0x0500,0x2454,0x7800,0x7F44,0x3800,0x3844,0x2800,0x3844,0x7F00,0x3854,0x5800,0x087E,0x0900,0x98A4,0x7C00,
              0x7F04,0x7800,0x047D,0x0000,0x4080,0x7D00,0x7F10,0x6C00,0x417F,0x4000,0x7C18,0x7C00,0x7C04,0x7800,0x3844,0x3800,
              0xFC24,0x1800,0x1824,0xFC80,0x7C04,0x0800,0x4854,0x2400,0x043E,0x4400,0x3C40,0x7C00,0x1C60,0x1C00,0x7C30,0x7C00,
              0x6C10,0x6C00,0x9CA0,0x7C00,0x6454,0x4C00,0x0836,0x4100,0x0077,0x0000,0x4136,0x0800,0x0201,0x0201,0x704C,0x7000 };
            return (force_builtin || !FontBase) ? font4x8[n] : emu.memory[ u16(FontBase+n) ];
        }
        u16 GetPalette(unsigned n, bool force_builtin=false) const
        {
            // This is our built-in palette. It is basically the same as the standard CGA/EGA/VGA 16-color palette.
            static const u16 palette[16] =
                { 0x000,0x00A,0x0A0,0x0AA, 0xA00,0xA0A,0xAA5,0xAAA, 0x555,0x55F,0x5F5,0x5FF, 0xF55,0xF5F,0xFF5,0xFFF };
            return (force_builtin || !PaletteBase) ? palette[n] : emu.memory[ u16(PaletteBase+n) ];
        }

        void Render(bool blinkyframe)
        {
            u8 pixbuf[WIDTH*HEIGHT];
            std::fill(pixbuf,pixbuf+WIDTH*HEIGHT, BorderColor);

            for(unsigned y=0; y<12; ++y)
                for(unsigned x=0; x<32; ++x)
                {
                    u16 v = emu.memory[ u16(RAMbase + x + y*32) ];
                    unsigned fg = v>>12, bg = (v>>8)&0xF, c = v&0x7F, blink=v&0x80;

                    unsigned font[2] = { GetFontCell(c*2+0), GetFontCell(c*2+1) };
                    if(blink && blinkyframe) fg = bg;
                    for(unsigned yp=0; yp<8; ++yp)
                        for(unsigned xp=0; xp<4; ++xp)
                            pixbuf[(y*8+yp+VBORDER)*WIDTH + HBORDER+x*4+xp] =
                                (font[xp/2] & (1 << (yp + 8*((xp&1)^1)))) ? fg : bg;
                }

            // Place the pixels in the screen surface, also scaling it in the process.
            constexpr int span=2048, wid = span*6/2048, ywid=wid, iqwid=wid*32/12, buf=wid*2;
            static float sines[17][span + buf*3], iqkernel[iqwid], Y[16];
            if(!cache_valid)
            {
                for(int x=0; x<span+buf*3; ++x) sines[0][x] = std::cos(x*3.141592653*2/wid);
                for(int p=0; p<16; ++p)
                {
                    unsigned a = GetPalette(p);
                    // Convert RGB into YIQ
                    Y[p]    = (0.299*(a >> 8) + 0.587*((a >> 4)&0xF) + 0.114*(a & 0xF))/15.;
                    float i = (0.596*(a >> 8) +-0.274*((a >> 4)&0xF) +-0.322*(a & 0xF));
                    float q = (0.211*(a >> 8) +-0.523*((a >> 4)&0xF) + 0.312*(a & 0xF));
                    // Save the luma in Y[p]; convert the chroma from polar into angular format
                    float d = std::hypot(q, i) / 15., A = std::atan2(q, i);
                    for(int x=0; x<span+buf*3; ++x) sines[1+p][x] = d * std::cos(x*3.141592653*2/wid + A - 0.5);
                }
                float iqtotal = 0;
                for(int i=0; i<iqwid; ++i) iqtotal += (iqkernel[i] = std::exp((i-iqwid/2)*(i-iqwid/2)/-128.f));
                for(int i=0; i<iqwid; ++i) iqkernel[i] *= (2.0 / iqtotal);
                cache_valid = true;
            }
            #pragma omp parallel for
            for(unsigned y=0; y<HEIGHT*SCALE; ++y)
            {
                int delta = ((y*240/(HEIGHT*SCALE))%3) * wid/5;
                float line[span + buf*3] = {0};
                // Generate scanline signal
                for(int x=0; x<span; ++x)
                {
                    auto p = pixbuf[ (x*WIDTH/span) + (y/SCALE)*WIDTH ];
                    line[buf + x] = Y[p]/std::max(1,int(Delay)-90) + sines[1+p][x+buf-delta]*(1-Delay/120.0);
                }
                // Decode scanline signal
                for(unsigned x=0; x<WIDTH*SCALE; ++x)
                {
                    int xx = x*span/(WIDTH*SCALE)+buf, xxy=xx-ywid/2, xxiq=xx-iqwid/2;
                    const float* iqsin = sines[0]+xxiq-delta, *yline=line+xxy, *iqline=line+xxiq;
                    float Y=0,I=0,Q=0;
                    for(int i=0; i<ywid; ++i)    Y += yline[i];
                    for(int i=0; i<iqwid; ++i) { float iq = iqline[i] * iqkernel[i];
                                                 I += iq * iqsin[i];
                                                 Q += iq * iqsin[i+wid/4]; }
                    int r; {int&k=r; k = ((I* 0.96f + Q*-0.62f) + Y/ywid)*255; if(k<0) k=0; if(k>255)k=255; }
                    int g; {int&k=g; k = ((I*-0.27f + Q*-0.65f) + Y/ywid)*255; if(k<0) k=0; if(k>255)k=255; }
                    int b; {int&k=b; k = ((I*-1.10f + Q* 1.70f) + Y/ywid)*255; if(k<0) k=0; if(k>255)k=255; }
                    ((u32*)(emu.s->pixels))[ y*WIDTH*SCALE + x] = (r<<16) + (g<<8) + b;
                }
            }

            // Refresh the screen.
            SDL_Flip(emu.s);
        }
    } hw_monitor{*this};

    class HardwareKeyboard: public HardwareBase // Generic Keyboard
    {
        u8 buffer[0x100], bufhead=0, buftail=0, state[0x100];
        unsigned Counter = 0;
    public:
        HardwareKeyboard(Emulator& e) : HardwareBase(e,0x30cf7406,1,0) {}
        virtual void Interrupt()
        {
            switch(emu.reg[A])
            {
                case 0: bufhead=buftail; break;
                case 1: emu.reg[C] = bufhead==buftail ? 0 : buffer[buftail++]; break;
                case 2: emu.reg[C] = emu.reg[B] < 0x100 && state[emu.reg[B]]; break;
                case 3: IRQ = emu.reg[B]; break;
            }
        }
        virtual void Tick()
        {
            for(Counter+=3; Counter >= 10000; Counter -= 10000) // Check keyboard events 50 times in a second
            {
                SDL_Event event = { };
                while(SDL_PollEvent( &event ))
                {
                    if(event.type == SDL_KEYDOWN || event.type == SDL_KEYUP)
                    {
                        u8 code = Translate(event.key.keysym);
                        state[code] = event.type == SDL_KEYDOWN;
                        if(code && event.type == SDL_KEYDOWN) buffer[bufhead++] = code;
                        if(IRQ) emu.Interrupt(IRQ, true);
                    }
                }
            }
        }
        // Translate SDL key symbol into DCPU key code.
        static u8 Translate(const SDL_keysym& key)
        {
            // Because SDL does not give the unicode value
            // for when key is released, cache them.
            static std::unordered_map<unsigned,unsigned> uni_map;
            unsigned& unicode = uni_map[key.scancode];
            if(key.unicode) unicode = key.unicode;
            switch(key.sym)
            {
                case SDLK_BACKSPACE: return 0x10;
                case SDLK_RETURN:    return 0x11;
                case SDLK_INSERT:    return 0x12;
                case SDLK_DELETE:    return 0x13;
                case SDLK_ESCAPE:    return 0x1B;
                case SDLK_UP:        return 0x80;
                case SDLK_DOWN:      return 0x81;
                case SDLK_LEFT:      return 0x82;
                case SDLK_RIGHT:     return 0x83;
                case SDLK_RSHIFT:    return 0x90;
                case SDLK_RCTRL:     return 0x91;
                case SDLK_LSHIFT:    return 0x90;
                case SDLK_LCTRL:     return 0x91;
                default: if(unicode >= 0x20 && unicode <= 0x7F)return unicode;
            }
            return 0;
        }
    } hw_keyboard{*this};

    std::vector<class HardwareBase*> Hardware {&hw_clock,&hw_monitor,&hw_keyboard};

    void tick(unsigned n=1)
    {
        while(n-- > 0) for(const auto& p: Hardware) p->Tick();
    }

    // Return a parameter to an instruction. Parameters are read-write,
    // i.e. the instruction may either read or write the register/memory location.
    template<char tag, bool skipping=false>
    u16& value(u16 v)
    {
        // When skipping=true, the return value is insignificant; it only matters
        // whether PC is incremented the right amount, and that there are no side effects.
        static u16 tmp;

        if(v == 0x18 && !skipping) return memory[tag == 'a' ? reg[SP]++ : --reg[SP]];

        if(v >= 0x20) return tmp = v-0x21;   // 20..3F, read-only immediate
        const auto specs = reg_specs[v];
        u16* val = nullptr; tmp = 0;
        if((specs & 0xFu) != noreg) tmp = *(val = &reg[specs & 0xFu]);
        if(specs & imm) { tick(); val = &(tmp += memory[reg[PC]++]); }
        if(specs & mem) return memory[tmp];
        return *val;

        // Literals are actually read-only. Because the instruction _may_
        // write into the target operand even if it is a literal, the
        // literals will be first stored into a temporary variable (tmp),
        // which can then be harmlessly overwritten.
    }

    #define PUSH value<'b'>(0x18)
    #define POP  value<'a'>(0x18)

    // Invokes interrupt with message "int_num". Queuing is always forced when invoked
    // by hardware, because launching an interrupt mid-instruction will do bad things.
    void Interrupt(u16 int_num, bool from_hardware = false)
    {
        if(!reg[IA]) return; // Interrupts disabled, discard the interrupt.
        if(IRQqueuing || from_hardware)
        {
            // Put interrupt in queue instead
            IRQqueue[ IRQhead++ ] = int_num;
            if( IRQhead == IRQtail ) { /* Queue overflow! TODO: Execute HCF instruction */ }
        }
        else
        {
            // Invoke interrupt if interrupts are allowed.
            PUSH = reg[PC]; reg[PC] = reg[IA];
            PUSH = reg[A];  reg[A] = int_num;
            IRQqueuing = true;
        }
    }

    template<bool skipping=false>
    void op()
    {
        tick();
        if(DisassemblyTrace && !skipping)
        {
            int l = std::fprintf(stderr, "%04X %04X|%s", reg[PC], memory[reg[PC]], Disassemble(reg[PC],memory).c_str());
            std::fprintf(stderr, "%*s", 50-l, "");
            for(int i=0; i<12; ++i) std::fprintf(stderr, " %s:%04X", regnames[i], reg[i]);
            std::fprintf(stderr, "\n");
        }
        // Read the next instruction from memory:
        u16 v = memory[reg[PC]++];
        // Parse the individual components of the instruction:
        // Instruction format: aaaaaa bbbbb ooooo
        u16 o = (v & 0x1F), bb = (v>>5) & 0x1F, aa = (v>>10) & 0x3F;
        // List of basic instructions. Chosen by the value of "o":
        enum { nbi,SET,ADD,SUB,MUL,MLI,DIV,DVI,MOD,MDI,AND,BOR,XOR,SHR,ASR,SHL,
               IFB,IFC,IFE,IFN,IFG,IFA,IFL,IFU,   ADX=0x1A,SBX,   STI=0x1E,STD };
        // List of non-basic instructions (nbi), chosen by the value of "bb":
        enum { JSR=0x21, HCF=0x27,INT,IAG,IAS,RFI,IAQ, HWN=0x30,HWQ,HWI };
        // Parse the two parameters. (Note: "b" is skipped when nbi.)
        u16& a =              value<'a',skipping>(aa); s32 sa = (s16)a, sr;
        u16& b = o==nbi ? v : value<'b',skipping>(bb); s32 sb = (s16)b; u32 wb = b, wr;
        // Then execute the instruction (unless the instruction is to be skipped).
        if(skipping) { if(o>=IFB && o<=IFU) op<true>(); } else
        switch(o==nbi ? bb+0x20 : o)
        {
            // A simple move. It can also be used as
            // a PUSH/POP/RET/JMP with properly chosen parameters.
            case SET: tick(0); b = a; break;
            // Simple binary operations.
            case AND: tick(0); b &= a; break;
            case BOR: tick(0); b |= a; break;
            case XOR: tick(0); b ^= a; break;
            // Fundamental arithmetic operations. The overflow is stored in the EX register.
            case ADD: tick(1); wr = b+a;    b = wr; reg[EX] = wr>>16; break;
            case SUB: tick(1); wr = b-a;    b = wr; reg[EX] = wr>>16; break;
            case ADX: tick(1); wr = b+a+reg[EX]; b = wr; reg[EX] = wr>>16; break;
            case SBX: tick(1); wr = b-a+reg[EX]; b = wr; reg[EX] = wr>>16; break;
            case MUL: tick(1); wr = wb*a;   b = wr; reg[EX] = wr>>16; break;
            case MLI: tick(1); sr = sb*sa;  b = sr; reg[EX] = sr>>16; break;
            // Bit-shifting left. EX stores the bits that were shifted out from the register.
            case SHL: tick(0); wr = wb<<a;  b = wr; reg[EX] = wr>>16; break;
            // Division and modulo. If the divisor is 0, result is 0.
            case MOD: tick(2); b  = a ?  b%a  : 0; break;
            case MDI: tick(2); b  = a ? sb%sa : 0; break;
            case DIV: tick(2); wr = a ?(wb<<16)/a :0; b = wr>>16; reg[EX] = wr; break;
            case DVI: tick(2); sr = sa?(sb<<16)/sa:0; b = sr>>16; reg[EX] = sr; break;
            // Bit-shifting right. EX stores the bits that were shifted out from the register.
            case SHR: tick(0); wr =    (wb<<16) >> a; b = wr>>16; reg[EX] = wr; break;
            case ASR: tick(0); sr =    (sb<<16) >> a; b = sr>>16; reg[EX] = sr; break;
            // Conditional execution. Skips the next instruction if the condition doesn't match.
            case IFE: tick(1); if(!(b == a)) op<true>(); break;
            case IFN: tick(1); if(!(b != a)) op<true>(); break;
            case IFG: tick(1); if(!(b >  a)) op<true>(); break;
            case IFB: tick(1); if(!(b &  a)) op<true>(); break;
            case IFA: tick(1); if(!(sb> sa)) op<true>(); break;
            case IFU: tick(1); if(!(sb< sa)) op<true>(); break;
            case IFL: tick(1); if(!(b <  a)) op<true>(); break;
            case IFC: tick(1); if( (b &  a)) op<true>(); break;
            // Streaming opcodes
            case STI: tick(1); b=a; ++reg[I]; ++reg[J]; break;
            case STD: tick(1); b=a; --reg[I]; --reg[J]; break;
            // Jump-To-Subroutine: Pushes the current program counter and chooses a new one.
            case JSR: tick(1); PUSH = reg[PC]; reg[PC] = a; break;
            // Interrupt control: Enabling, issuing, queuing, and returning from.
            case IAG: tick(0); a = reg[IA]; break;
            case IAS: tick(0); reg[IA] = a; break;
            case INT: tick(3); Interrupt(a); break;
            case IAQ: tick(1); IRQqueuing = a; break;
            case RFI: tick(2); IRQqueuing = false; reg[A] = POP; reg[PC] = POP; break;
            // Hardware: Count of devices, information query, and requests.
            case HWN: tick(1); a = Hardware.size(); break;
            case HWQ: tick(3); if(a < Hardware.size()) Hardware[a]->Query(); break;
            case HWI: tick(3); if(a < Hardware.size()) Hardware[a]->Interrupt(); break;
            case HCF: break; /* TODO: Halt and Catch Fire */
            // Anything else is invalid.
            default: std::fprintf(stderr, "Invalid opcode %04X at PC=%X\n", v, reg[PC]);
        }
    }

    #undef PUSH
    #undef POP
public:
    void run()
    {
        // Infinite loop!
        for(;;)
        {
            if(!IRQqueuing && IRQhead != IRQtail)
            {
                 // Trigger a queued interrupt if possible.
                 auto intno = IRQqueue[ IRQtail++ ];
                 Interrupt(intno);
            }
            op();
        }
    }
};

int main()
{
    // Declare the emulator.
    Emulator emu;
    // Load the initial RAM contents.
    char Buf[131072];
    int len = std::fread(Buf, 1, sizeof(Buf), stdin);

    std::vector<u16> mem = Assembler( {Buf,Buf+len} );
    std::copy(mem.begin(), mem.end(), std::begin(emu.memory));
    //std::fwrite(emu.memory, 0x10000, 2, stdout);
    // Launch the emulator.
    emu.run();
}


