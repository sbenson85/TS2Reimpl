// A minimal vs_1_1 assembler and interpreter for the draw2d tests (lane OU): turns a vertex shader's assembly text into a
// behavioural digest, so the original's text and ours can be compared by what they do rather than how they are written.
//
// How equivalence of the two shader texts is established (notes/decisions.md, 2026-10-28: our own shader source): the
// digest is made from (1) the declared inputs (usage name, usage index, input register), sorted by register, and (2) what
// the program writes to every output register (oPos, oD0, oT0) for a fixed set of input vectors and constant registers,
// per component actually written (the write mask is part of the digest, an unwritten component is not compared). Two
// texts with the same digest declare the same inputs and compute the same outputs from the same constants (c0, c1 are
// the ones the renderer fills). Temporary register numbers, comments, spacing and the instruction order that produces the
// same values do not matter. The interpreter knows exactly the instructions the renderer's shader may use (mov, add,
// sub, mul, mad, dp3, dp4, min, max); any other instruction or malformed line makes the digest's first word 0, so a text
// the test cannot judge fails the comparison instead of passing it.
//
// The floating-point operations are IEEE single precision in the order the text gives them; mul then add is not fused.
#pragma once

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <algorithm>
#include <bit>
#include <string>
#include <vector>

namespace vsdigest {

namespace detail {

struct Vec {
    float c[4];
};

struct Operand {
    char file = 0;        // 'v' input, 'c' constant, 'r' temporary, 'P' oPos, 'D' oD, 'T' oT
    unsigned index = 0;
    bool negate = false;
    unsigned swizzle[4] = {0, 1, 2, 3};
    unsigned mask = 0xf; // destination
};

inline bool ParseComponents(const std::string &s, unsigned out[4], unsigned *mask)
{
    unsigned n = 0, m = 0;
    for (char ch : s) {
        const char *at = std::strchr("xyzw", ch);
        if (!at || !*at || n == 4) return false;
        const unsigned comp = unsigned(at - "xyzw");
        if (mask) {
            if (m & (1u << comp)) return false;
            m |= 1u << comp;
        } else {
            out[n] = comp;
        }
        ++n;
    }
    if (n == 0) return false;
    if (!mask)
        for (unsigned i = n; i < 4; ++i) out[i] = out[n - 1];
    if (mask) *mask = m;
    return true;
}

inline bool ParseOperand(std::string t, bool destination, Operand &op)
{
    if (!t.empty() && t[0] == '-' && !destination) {
        op.negate = true;
        t.erase(0, 1);
    }
    std::string head = t, comps;
    const size_t dot = t.find('.');
    if (dot != std::string::npos) {
        head = t.substr(0, dot);
        comps = t.substr(dot + 1);
    }
    auto number = [](const std::string &s, size_t from, unsigned &out) {
        if (from >= s.size()) return false;
        unsigned v = 0;
        for (size_t i = from; i < s.size(); ++i) {
            if (s[i] < '0' || s[i] > '9') return false;
            v = v * 10 + unsigned(s[i] - '0');
        }
        out = v;
        return true;
    };
    if (head == "oPos") {
        op.file = 'P';
    } else if (head.size() >= 3 && head.compare(0, 2, "oD") == 0) {
        op.file = 'D';
        if (!number(head, 2, op.index)) return false;
    } else if (head.size() >= 3 && head.compare(0, 2, "oT") == 0) {
        op.file = 'T';
        if (!number(head, 2, op.index)) return false;
    } else if (head.size() >= 2 && (head[0] == 'v' || head[0] == 'c' || head[0] == 'r')) {
        op.file = head[0];
        if (!number(head, 1, op.index)) return false;
    } else {
        return false;
    }
    if (destination) {
        if (op.file == 'v' || op.file == 'c') return false;
        if (!comps.empty() && !ParseComponents(comps, nullptr, &op.mask)) return false;
    } else {
        if (op.file == 'P' || op.file == 'D' || op.file == 'T') return false;
        if (!comps.empty() && !ParseComponents(comps, op.swizzle, nullptr)) return false;
    }
    return true;
}

struct Instruction {
    std::string op;
    Operand dest, src[3];
    unsigned sources = 0;
};

struct Decl {
    std::string usage;
    unsigned usageIndex, reg;
};

struct Program {
    bool ok = false;
    std::vector<Decl> decls;
    std::vector<Instruction> code;
};

inline std::vector<std::string> SplitOperands(const std::string &rest)
{
    std::vector<std::string> out;
    std::string cur;
    for (char ch : rest) {
        if (ch == ',') {
            out.push_back(cur);
            cur.clear();
        } else if (ch != ' ' && ch != '\t' && ch != '\r') {
            cur += ch;
        }
    }
    out.push_back(cur);
    return out;
}

inline Program Parse(const char *text, size_t size)
{
    Program p;
    std::string all(text, strnlen(text, size));
    size_t pos = 0;
    bool header = false;
    while (pos <= all.size()) {
        size_t end = all.find('\n', pos);
        if (end == std::string::npos) end = all.size();
        std::string line = all.substr(pos, end - pos);
        pos = end + 1;
        const size_t comment = (std::min)(line.find(';'), line.find("//"));
        if (comment != std::string::npos) line.erase(comment);
        size_t a = 0, b = line.size();
        while (a < b && (line[a] == ' ' || line[a] == '\t' || line[a] == '\r')) ++a;
        while (b > a && (line[b - 1] == ' ' || line[b - 1] == '\t' || line[b - 1] == '\r')) --b;
        line = line.substr(a, b - a);
        if (line.empty()) continue;
        if (!header) {
            if (line != "vs_1_1") return p;
            header = true;
            continue;
        }
        const size_t space = line.find_first_of(" \t");
        const std::string word = line.substr(0, space);
        const std::string rest = space == std::string::npos ? "" : line.substr(space + 1);
        if (word.compare(0, 4, "dcl_") == 0) {
            std::string usage = word.substr(4);
            unsigned index = 0;
            size_t digits = usage.size();
            while (digits > 0 && usage[digits - 1] >= '0' && usage[digits - 1] <= '9') --digits;
            if (digits < usage.size()) index = unsigned(std::atoi(usage.c_str() + digits));
            usage.resize(digits);
            const std::vector<std::string> parts = SplitOperands(rest);
            Operand reg;
            if (parts.size() != 1 || !ParseOperand(parts[0], false, reg) || reg.file != 'v') return p;
            p.decls.push_back({usage, index, reg.index});
            continue;
        }
        Instruction in;
        in.op = word;
        unsigned sources = 0;
        if (word == "mov") sources = 1;
        else if (word == "add" || word == "sub" || word == "mul" || word == "dp3" || word == "dp4" || word == "min" ||
                 word == "max") sources = 2;
        else if (word == "mad") sources = 3;
        else return p;
        const std::vector<std::string> parts = SplitOperands(rest);
        if (parts.size() != sources + 1) return p;
        if (!ParseOperand(parts[0], true, in.dest)) return p;
        for (unsigned i = 0; i < sources; ++i)
            if (!ParseOperand(parts[1 + i], false, in.src[i])) return p;
        in.sources = sources;
        p.code.push_back(in);
    }
    p.ok = header;
    return p;
}

struct Machine {
    Vec v[16] = {}, c[96] = {}, r[12] = {}, pos = {}, d[2] = {}, t[8] = {};
    unsigned posMask = 0, dMask[2] = {}, tMask[8] = {};
};

inline Vec Read(const Machine &m, const Operand &o)
{
    const Vec *file = o.file == 'v' ? m.v : o.file == 'c' ? m.c : m.r;
    const unsigned limit = o.file == 'v' ? 16 : o.file == 'c' ? 96 : 12;
    Vec raw = o.index < limit ? file[o.index] : Vec{};
    Vec out;
    for (unsigned i = 0; i < 4; ++i) out.c[i] = o.negate ? -raw.c[o.swizzle[i]] : raw.c[o.swizzle[i]];
    return out;
}

inline void Write(Machine &m, const Operand &o, const Vec &value)
{
    Vec *target = nullptr;
    unsigned *mask = nullptr;
    switch (o.file) {
    case 'r': if (o.index < 12) target = &m.r[o.index]; break;
    case 'P': target = &m.pos; mask = &m.posMask; break;
    case 'D': if (o.index < 2) { target = &m.d[o.index]; mask = &m.dMask[o.index]; } break;
    case 'T': if (o.index < 8) { target = &m.t[o.index]; mask = &m.tMask[o.index]; } break;
    }
    if (!target) return;
    for (unsigned i = 0; i < 4; ++i)
        if (o.mask & (1u << i)) target->c[i] = value.c[i];
    if (mask) *mask |= o.mask;
}

inline void Execute(const Program &p, Machine &m)
{
    for (const Instruction &in : p.code) {
        const Vec a = Read(m, in.src[0]);
        const Vec b = in.sources > 1 ? Read(m, in.src[1]) : Vec{};
        const Vec c = in.sources > 2 ? Read(m, in.src[2]) : Vec{};
        Vec out{};
        if (in.op == "mov") out = a;
        else if (in.op == "dp3" || in.op == "dp4") {
            float sum = 0;
            for (unsigned i = 0; i < (in.op == "dp3" ? 3u : 4u); ++i) sum += a.c[i] * b.c[i];
            for (float &x : out.c) x = sum;
        } else {
            for (unsigned i = 0; i < 4; ++i) {
                if (in.op == "add") out.c[i] = a.c[i] + b.c[i];
                else if (in.op == "sub") out.c[i] = a.c[i] - b.c[i];
                else if (in.op == "mul") out.c[i] = a.c[i] * b.c[i];
                else if (in.op == "min") out.c[i] = a.c[i] < b.c[i] ? a.c[i] : b.c[i];
                else if (in.op == "max") out.c[i] = a.c[i] >= b.c[i] ? a.c[i] : b.c[i];
                else {
                    const float product = a.c[i] * b.c[i];
                    out.c[i] = product + c.c[i];
                }
            }
        }
        Write(m, in.dest, out);
    }
}

inline uint32_t Fold(const std::string &s)
{
    uint32_t h = 2166136261u;
    for (char ch : s) h = (h ^ uint8_t(ch)) * 16777619u;
    return h;
}

} // namespace detail

// The digest of the text: word 0 is 1 when the text is understood, then the declarations (register order) and the
// outputs for a fixed set of inputs.
inline std::vector<uint32_t> Digest(const char *text, size_t size)
{
    using namespace detail;
    const Program p = Parse(text, size);
    std::vector<uint32_t> words{p.ok ? 1u : 0u};
    if (!p.ok) return words;
    std::vector<Decl> decls = p.decls;
    std::sort(decls.begin(), decls.end(), [](const Decl &x, const Decl &y) { return x.reg < y.reg; });
    words.push_back(uint32_t(decls.size()));
    for (const Decl &d : decls) words.insert(words.end(), {Fold(d.usage), d.usageIndex, d.reg});
    uint32_t seed = 0x1234567u;
    auto next = [&seed] {
        seed = seed * 1664525u + 1013904223u;
        return float(int32_t(seed >> 8) % 2001 - 1000) / 8.0f;
    };
    for (unsigned vector = 0; vector < 6; ++vector) {
        Machine m;
        for (unsigned reg = 0; reg < 16; ++reg)
            for (float &x : m.v[reg].c) x = next();
        for (unsigned reg = 0; reg < 96; ++reg)
            for (float &x : m.c[reg].c) x = next();
        Execute(p, m);
        auto push = [&words](const Vec &v, unsigned mask) {
            words.push_back(mask);
            for (unsigned i = 0; i < 4; ++i) words.push_back(mask & (1u << i) ? std::bit_cast<uint32_t>(v.c[i]) : 0u);
        };
        push(m.pos, m.posMask);
        for (unsigned i = 0; i < 2; ++i) push(m.d[i], m.dMask[i]);
        for (unsigned i = 0; i < 8; ++i) push(m.t[i], m.tMask[i]);
    }
    return words;
}

} // namespace vsdigest
