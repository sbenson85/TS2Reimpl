// Reads a PDB of our own build (DIA SDK) and prints, one JSON object per line, the facts our compiler knows about
// our code: the layout of each class or struct (size, fields with offsets and types, base classes) and the
// signature of each function (calling convention, this, return and parameter types). tools/annotate.py turns
// them into Ghidra annotations for the original classes our code mirrors and the functions our hooks replace.
//
//   pdbfacts.exe <file.pdb>  > local/pdbfacts.jsonl
//
// Types are written in db/annotations.jsonl's notation: i8 u8 i16 u16 i32 u32 i64 u64 f32 f64 bool void,
// T* for pointers and references, T[n] for arrays, a class or struct by its name, "fnptr" for a pointer to a
// function, and "?<size>" for anything else (an enum is written as its underlying integer).

#include <windows.h>

#include <dia2.h>
#include <diacreate.h>

#include <cstdio>
#include <set>
#include <string>

namespace {

std::string Narrow(const wchar_t *w)
{
    if (!w) return {};
    const int n = WideCharToMultiByte(CP_UTF8, 0, w, -1, nullptr, 0, nullptr, nullptr);
    std::string s(n > 0 ? n - 1 : 0, '\0');
    if (n > 1) WideCharToMultiByte(CP_UTF8, 0, w, -1, s.data(), n, nullptr, nullptr);
    return s;
}

std::string Json(const std::string &s)
{
    std::string out = "\"";
    for (const char c : s) {
        if (c == '"' || c == '\\') out += '\\';
        if (static_cast<unsigned char>(c) < 0x20) continue;
        out += c;
    }
    return out + "\"";
}

std::string Name(IDiaSymbol *sym)
{
    BSTR b = nullptr;
    if (sym->get_name(&b) != S_OK) return {};
    std::string s = Narrow(b);
    SysFreeString(b);
    return s;
}

DWORD Tag(IDiaSymbol *sym)
{
    DWORD t = 0;
    sym->get_symTag(&t);
    return t;
}

ULONGLONG Length(IDiaSymbol *sym)
{
    ULONGLONG n = 0;
    sym->get_length(&n);
    return n;
}

std::string BaseType(DWORD bt, ULONGLONG len)
{
    switch (bt) {
    case btVoid:
        return "void";
    case btBool:
        return "bool";
    case btFloat:
        return len == 8 ? "f64" : "f32";
    case btChar:
    case btInt:
    case btLong:
        return len == 1 ? "i8" : len == 2 ? "i16" : len == 8 ? "i64" : "i32";
    case btUInt:
    case btULong:
    case btWChar:
    case btChar16:
    case btChar32:
    case btChar8:
        return len == 1 ? "u8" : len == 2 ? "u16" : len == 8 ? "u64" : "u32";
    default:
        return "?" + std::to_string(len);
    }
}

std::string TypeString(IDiaSymbol *type)
{
    if (!type) return "void";
    switch (Tag(type)) {
    case SymTagBaseType: {
        DWORD bt = 0;
        type->get_baseType(&bt);
        return BaseType(bt, Length(type));
    }
    case SymTagPointerType: {
        IDiaSymbol *inner = nullptr;
        type->get_type(&inner);
        std::string s = inner && Tag(inner) == SymTagFunctionType ? "fnptr" : TypeString(inner) + "*";
        if (inner) inner->Release();
        return s;
    }
    case SymTagArrayType: {
        IDiaSymbol *inner = nullptr;
        type->get_type(&inner);
        DWORD count = 0;
        type->get_count(&count);
        std::string s = TypeString(inner) + "[" + std::to_string(count) + "]";
        if (inner) inner->Release();
        return s;
    }
    case SymTagUDT:
        return Name(type);
    case SymTagEnum: {
        IDiaSymbol *inner = nullptr;
        type->get_type(&inner);
        std::string s = TypeString(inner);
        if (inner) inner->Release();
        return s;
    }
    case SymTagTypedef: {
        IDiaSymbol *inner = nullptr;
        type->get_type(&inner);
        std::string s = TypeString(inner);
        if (inner) inner->Release();
        return s;
    }
    default:
        return "?" + std::to_string(Length(type));
    }
}

template <class Fn> void Children(IDiaSymbol *parent, enum SymTagEnum tag, Fn fn)
{
    IDiaEnumSymbols *list = nullptr;
    if (parent->findChildren(tag, nullptr, nsNone, &list) != S_OK || !list) return;
    IDiaSymbol *child = nullptr;
    ULONG got = 0;
    while (list->Next(1, &child, &got) == S_OK && got == 1) {
        fn(child);
        child->Release();
    }
    list->Release();
}

void Udt(IDiaSymbol *udt, std::set<std::string> &seen)
{
    const std::string name = Name(udt);
    const ULONGLONG size = Length(udt);
    if (name.empty() || size == 0 || !seen.insert(name).second) return;
    DWORD kind = 0;
    udt->get_udtKind(&kind);
    std::string fields, bases;
    Children(udt, SymTagData, [&](IDiaSymbol *d) {
        DWORD loc = 0;
        d->get_locationType(&loc);
        if (loc != LocIsThisRel && loc != LocIsBitField) return; // static members and constants are not layout
        LONG off = 0;
        d->get_offset(&off);
        IDiaSymbol *type = nullptr;
        d->get_type(&type);
        std::string f = "{\"off\":" + std::to_string(off) + ",\"name\":" + Json(Name(d)) + ",\"type\":" + Json(TypeString(type));
        if (type) type->Release();
        if (loc == LocIsBitField) {
            DWORD pos = 0;
            d->get_bitPosition(&pos);
            f += ",\"bits\":[" + std::to_string(pos) + "," + std::to_string(Length(d)) + "]";
        }
        fields += (fields.empty() ? "" : ",") + f + "}";
    });
    Children(udt, SymTagBaseClass, [&](IDiaSymbol *b) {
        LONG off = 0;
        b->get_offset(&off);
        BOOL isVirtual = FALSE;
        b->get_virtualBaseClass(&isVirtual);
        bases += (bases.empty() ? "" : ",") + std::string("{\"name\":") + Json(Name(b)) + ",\"off\":" + std::to_string(off) +
                 (isVirtual ? ",\"virtual\":true" : "") + "}";
    });
    BOOL hasVtable = FALSE;
    IDiaSymbol *shape = nullptr;
    if (udt->get_virtualTableShape(&shape) == S_OK && shape) {
        hasVtable = TRUE;
        shape->Release();
    }
    std::printf("{\"udt\":%s,\"kind\":\"%s\",\"size\":%llu,\"vfptr\":%s,\"bases\":[%s],\"fields\":[%s]}\n", Json(name).c_str(),
                kind == UdtUnion ? "union" : kind == UdtClass ? "class" : "struct", size, hasVtable ? "true" : "false", bases.c_str(),
                fields.c_str());
}

const char *Convention(DWORD cc)
{
    switch (cc) {
    case CV_CALL_NEAR_C:
        return "cdecl";
    case CV_CALL_NEAR_STD:
        return "stdcall";
    case CV_CALL_NEAR_FAST:
        return "fastcall";
    case CV_CALL_THISCALL:
        return "thiscall";
    default:
        return "other";
    }
}

void Function(IDiaSymbol *fn, std::set<std::string> &seen)
{
    const std::string name = Name(fn);
    IDiaSymbol *type = nullptr;
    if (name.empty() || fn->get_type(&type) != S_OK || !type) return;
    DWORD cc = 0;
    type->get_callingConvention(&cc);
    IDiaSymbol *ret = nullptr;
    type->get_type(&ret);
    std::string params;
    Children(type, SymTagFunctionArgType, [&](IDiaSymbol *a) {
        IDiaSymbol *t = nullptr;
        a->get_type(&t);
        params += (params.empty() ? "" : ",") + Json(TypeString(t));
        if (t) t->Release();
    });
    IDiaSymbol *self = nullptr;
    std::string thisType = "null";
    if (type->get_objectPointerType(&self) == S_OK && self) {
        thisType = Json(TypeString(self));
        self->Release();
    }
    std::string names; // the parameters' names, from the function's own symbols (the type has only their types)
    Children(fn, SymTagData, [&](IDiaSymbol *d) {
        DWORD kind = 0;
        if (d->get_dataKind(&kind) == S_OK && kind == DataIsParam) names += (names.empty() ? "" : ",") + Json(Name(d));
    });
    const std::string line = "{\"fn\":" + Json(name) + ",\"cc\":\"" + Convention(cc) + "\",\"this\":" + thisType + ",\"ret\":" +
                             Json(TypeString(ret)) + ",\"params\":[" + params + "],\"names\":[" + names + "]}";
    if (ret) ret->Release();
    type->Release();
    if (seen.insert(line).second) std::printf("%s\n", line.c_str());
}

} // namespace

int wmain(int argc, wchar_t **argv)
{
    if (argc != 2) {
        std::fprintf(stderr, "usage: pdbfacts <file.pdb>\n");
        return 2;
    }
    if (FAILED(CoInitialize(nullptr))) return 1;
    IDiaDataSource *source = nullptr;
    HRESULT hr = NoRegCoCreate(L"msdia140.dll", CLSID_DiaSource, IID_IDiaDataSource, reinterpret_cast<void **>(&source));
    if (FAILED(hr)) {
        std::fprintf(stderr, "pdbfacts: cannot load msdia140.dll (0x%08lx)\n", hr);
        return 1;
    }
    IDiaSession *session = nullptr;
    IDiaSymbol *global = nullptr;
    if (FAILED(source->loadDataFromPdb(argv[1])) || FAILED(source->openSession(&session)) || FAILED(session->get_globalScope(&global))) {
        std::fprintf(stderr, "pdbfacts: cannot open %ls\n", argv[1]);
        return 1;
    }
    std::set<std::string> udts, fns;
    Children(global, SymTagUDT, [&](IDiaSymbol *u) { Udt(u, udts); });
    Children(global, SymTagCompiland, [&](IDiaSymbol *c) { Children(c, SymTagFunction, [&](IDiaSymbol *f) { Function(f, fns); }); });
    global->Release();
    session->Release();
    source->Release();
    CoUninitialize();
    return 0;
}
