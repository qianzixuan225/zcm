#include <iostream>
#include <sstream>
#include <set>

#include "Common.hpp"
#include "Emitter.hpp"
#include "GetOpt.hpp"
#include "ZCMGen.hpp"

#include "util/StringUtil.hpp"
#include "util/FileUtil.hpp"

using namespace std;

void setupOptionsJulia(GetOpt& gopt)
{
    gopt.addString(0, "julia-path", ".", "Julia destination directory");
}

// Some types do not have a 1:1 mapping from zcm types to native Julia storage types.
static string mapTypeName(const string& t)
{
    if      (t == "int8_t")   return "Int8";
    else if (t == "int16_t")  return "Int16";
    else if (t == "int32_t")  return "Int32";
    else if (t == "int64_t")  return "Int64";
    else if (t == "byte")     return "UInt8";
    else if (t == "float")    return "Float32";
    else if (t == "double")   return "Float64";
    else if (t == "string")   return "String";
    else if (t == "boolean")  return "Bool";
    else {
        return t;
    }
}

struct EmitJulia : public Emitter
{
    ZCMGen& zcm;
    ZCMStruct& ls;

    string hton, ntoh;

    EmitJulia(ZCMGen& zcm, ZCMStruct& ls, const string& fname):
        Emitter(fname), zcm(zcm), ls(ls),
        hton(zcm.gopt->getBool("little-endian-encoding") ? "htol" : "hton"),
        ntoh(zcm.gopt->getBool("little-endian-encoding") ? "ltoh" : "ntoh")
        {}

    void emitAutoGeneratedWarning()
    {
        emit(0, "# THIS IS AN AUTOMATICALLY GENERATED FILE.  DO NOT MODIFY");
        emit(0, "# BY HAND!!");
        emit(0, "#");
        emit(0, "# Generated by zcm-gen");
        emit(0, "#");
        emit(0, "");
    }

    void emitComment(int indent, const string& comment)
    {
        if (comment == "")
            return;

        auto lines = StringUtil::split(comment, '\n');
        if (lines.size() == 1) {
            emit(indent, "# %s", lines[0].c_str());
        } else {
            for (auto& line : lines) {
                emitStart(indent, "#");
                if (line.size() > 0)
                    emitContinue("%s", line.c_str());
                emitEnd("");
            }
        }
    }

    string topLevelPackage(const string& package) {
        auto packageParts = StringUtil::split(package, '.');
        if (packageParts.size() > 0) {
            return packageParts.at(0);
        } else {
            return "";
        }
    }

    void emitDependencies()
    {
        unordered_set<string> imports;
        string lsPackage = topLevelPackage(ls.structname.package);
        if (lsPackage != "") {
            imports.insert("import " + lsPackage);
        }

        for (auto& lm : ls.members) {
            auto& tn = lm.type.fullname;
            if (!ZCMGen::isPrimitiveType(tn) &&
                imports.find(tn) == imports.end() &&
                tn != ls.structname.fullname) {
                string lmPackage = topLevelPackage(lm.type.package);
                if (lmPackage != "") {
                    // The dependent type belongs to some package
                    if (lmPackage == lsPackage) {
                        // Then this is a dependency on a package from somewhere
                        // in the same module, so we should import its outer
                        // Julia module
                        imports.insert("import _" + lm.type.nameUnderscore());
                    } else {
                        // Then this is a dependency on some other package, so
                        // we can just import the entire other package:
                        imports.insert("import " + lmPackage);
                    }
                } else {
                    // The dependent type does not belong to any package, so
                    // we just import it from its outer module
                    imports.insert("import _" + lm.type.shortname + ": " + lm.type.shortname);
                }
            }
        }

        if (lsPackage != "") {
            // The type we're creating is in a package, so we need to @eval its
            // definition into the right module
            emit(0, "import %s", lsPackage.c_str());
            emit(0, "@eval %s begin", ls.structname.package.c_str());
        } else {
            // The type we're creating is not in a package, so we don't need to
            // do anything. But we'll create a "begin" block so that the number
            // of terminating "end" statements is the same no matter which path
            // was chosen here.
            emit(0, "begin");
        }

        for (auto& tn : imports) {
            // Add all the necessary imports
            emit(0, tn.c_str());
        }
    }

    void emitModuleStart()
    {
        emitAutoGeneratedWarning();
        emit(0, "module _%s", ls.structname.nameUnderscoreCStr());
        emit(0, "");
    }

    void emitModuleEnd()
    {
        const char *sn = ls.structname.nameUnderscoreCStr();
        emit(0, "end # from the `begin` block above");
        emit(0, "end # module _%s;", sn);
    }

    void emitInstance()
    {
        const char *sn = ls.structname.shortname.c_str();

        emitDependencies();

        // define the class
        emitComment(0, ls.comment);
        emit(0, "import ZCM");
        emit(0, "type %s <: ZCM.AbstractZCMType", sn);
        emit(0, "");

        // data members
        if (ls.members.size() > 0) {
            emit(1, "# **********************");
            emit(1, "# Members");
            emit(1, "# **********************");
            for (auto& lm : ls.members) {
                auto& mtn = lm.type.fullname;
                emitComment(2, lm.comment);
                string mappedTypename = mapTypeName(mtn);
                int ndim = (int)lm.dimensions.size();
                if (ndim == 0) {
                    emit(1, "%-30s::%s", lm.membername.c_str(), mappedTypename.c_str());
                } else {
                    emit(1, "%-30s::Array{%s,%u}", lm.membername.c_str(),
                                                   mappedTypename.c_str(), ndim);
                }
            }
            emit(0, "");
        }

        // constants
        if (ls.constants.size() > 0) {
            emit(0, "");
            emit(1, "# **********************");
            emit(1, "# Constants");
            emit(1, "# **********************");
            for (auto& lc : ls.constants) {
                assert(ZCMGen::isLegalConstType(lc.type));
                string mt = mapTypeName(lc.type);
                emit(1, "%-30s::%s", lc.membername.c_str(), mt.c_str(), lc.valstr.c_str());
            }
            emit(0, "");
        }

        emit(0, "");
        emit(1, "function %s()", sn);
        emit(0, "");
        emit(2, "self = new();");
        emit(0, "");

        // data members
        if (ls.members.size() > 0) {
            emit(2, "# **********************");
            emit(2, "# Members");
            emit(2, "# **********************");
            for (size_t i = 0; i < ls.members.size(); ++i) {
                auto& lm = ls.members[i];
                emitStart(2, "self.%s = ", lm.membername.c_str());
                emitMemberInitializer(lm, 0);
                emitEnd("");
            }
            emit(0, "");
        }

        // constants
        if (ls.constants.size() > 0) {
            emit(2, "# **********************");
            emit(2, "# Constants");
            emit(2, "# **********************");

            for (auto& lc : ls.constants) {
                assert(ZCMGen::isLegalConstType(lc.type));
                string mt = mapTypeName(lc.type);
                emitStart(2, "self.%s::%s = ", lc.membername.c_str(), mt.c_str());
                if (lc.isFixedPoint())
                    emitEnd("reinterpret(%s,%s)", mt.c_str(), lc.valstr.c_str());
                else
                    emitEnd("%s", lc.valstr.c_str());
            }
            emit(0, "");
        }

        emit(2, "return self");
        emit(1, "end");
        emit(0, "");
        emit(0, "end");
        emit(0, "");
    }

    void emitMemberInitializer(ZCMMember& lm, int dimNum)
    {
        auto& mtn = lm.type.fullname;
        string mappedTypename = mapTypeName(mtn);

        if ((size_t)dimNum == lm.dimensions.size()) {
            auto& tn = lm.type.fullname;
            const char* initializer = nullptr;
            if (tn == "byte") initializer = "0";
            else if (tn == "boolean") initializer = "false";
            else if (tn == "int8_t")  initializer = "0";
            else if (tn == "int16_t") initializer = "0";
            else if (tn == "int32_t") initializer = "0";
            else if (tn == "int64_t") initializer = "0";
            else if (tn == "float")   initializer = "0.0";
            else if (tn == "double")  initializer = "0.0";
            else if (tn == "string")  initializer = "\"\"";

            if (initializer) {
                fprintfPass("%s", initializer);
            } else {
                fprintfPass("%s()", tn.c_str());
            }
            return;
        }
        auto& dim = lm.dimensions[dimNum];
        if (dim.mode == ZCM_VAR) {
            size_t dimLeft = lm.dimensions.size() - dimNum;
            fprintfPass("Array{%s,%lu}(", mappedTypename.c_str(), dimLeft);
            for (size_t i = 0; i < dimLeft - 1; ++i)
                fprintfPass("0,");
            fprintfPass("0)");
        } else {
            fprintfPass("[ ");
            emitMemberInitializer(lm, dimNum+1);
            fprintfPass(" for dim%d in range(1,%s) ]", dimNum, dim.size.c_str());
        }
    }

    void emitGetHash()
    {
        auto* sn = ls.structname.shortname.c_str();

        emit(0, "const __%s_hash = Ref(Int64(0))", sn);

        emit(0, "function ZCM._get_hash_recursive(::Type{%s}, parents::Array{String})", sn);
        emit(1,     "if __%s_hash[] != 0; return __%s_hash[]; end", sn, sn);
        emit(1,     "if \"%s\" in parents; return 0; end", sn);
        for (auto& lm : ls.members) {
            if (!ZCMGen::isPrimitiveType(lm.type.fullname)) {
                emit(1, "newparents::Array{String} = [parents[:]; \"%s\"::String];", sn);
                break;
            }
        }
        emitStart(1, "hash::UInt64 = 0x%" PRIx64, ls.hash);
        for (auto &lm : ls.members) {
            if (!ZCMGen::isPrimitiveType(lm.type.fullname)) {
                auto *mtn = lm.type.nameUnderscoreCStr();
                emitContinue("+ reinterpret(UInt64,"
                                           "ZCM._get_hash_recursive(%s, newparents))", mtn);
            }
        }
        emitEnd("");

        emit(1,     "hash = (hash << 1) + ((hash >>> 63) & 0x01)");
        emit(1,     "__%s_hash[] = reinterpret(Int64, hash)", sn);
        emit(1,     "return __%s_hash[]", sn);
        emit(0, "end");
        emit(0, "");
        emit(0, "function ZCM.getHash(::Type{%s})", sn);
        emit(1,     "return ZCM._get_hash_recursive(%s, Array{String,1}())", sn);
        emit(0, "end");
        emit(0, "");
    }

    void emitEncodeSingleMember(ZCMMember& lm, const string& accessor_, int indent)
    {
        const string& tn = lm.type.fullname;
        auto *accessor = accessor_.c_str();

        if (tn == "string") {
            emit(indent, "write(buf, %s(UInt32(length(%s) + 1)))", hton.c_str(), accessor);
            emit(indent, "write(buf, %s)", accessor);
            emit(indent, "write(buf, 0)");
        } else if (tn == "boolean") {
            emit(indent, "write(buf, %s)", accessor);
        } else if (tn == "byte"    || tn == "int8_t"  ||
                   tn == "int16_t" || tn == "int32_t" || tn == "int64_t" ||
                   tn == "float"   || tn == "double") {
            emit(indent, "write(buf, %s(%s))", hton.c_str(), accessor);
        } else {
            emit(indent, "ZCM._encode_one(%s,buf)", accessor);
        }
    }

    void emitEncodeListMember(ZCMMember& lm, const string& accessor_, int indent,
                              const string& len_, int fixedLen)
    {
        auto& tn = lm.type.fullname;
        auto *accessor = accessor_.c_str();
        auto *len = len_.c_str();

        if (tn == "byte" || tn == "boolean" || tn == "int8_t" ||
            tn == "int16_t" || tn == "int32_t" || tn == "int64_t" ||
            tn == "float"  || tn == "double") {
            if (tn != "boolean")
                emit(indent, "for i in range(1,%s%s) %s[i] = %s(%s[i]) end",
                             (fixedLen ? "" : "msg."), len, accessor, hton.c_str(), accessor);
            emit(indent, "write(buf, %s[1:%s%s])",
                 accessor, (fixedLen ? "" : "msg."), len);
            return;
        } else {
            assert(0);
        }
    }

    void emitEncodeOne()
    {
        auto* sn = ls.structname.shortname.c_str();

        emit(0, "function ZCM._encode_one(msg::%s, buf)", sn);
        if (ls.members.size() == 0) {
            emit(1, "return nothing");
            emit(0, "end");
            return;
        }

        for (auto& lm : ls.members) {
            if (lm.dimensions.size() == 0) {
                emitEncodeSingleMember(lm, "msg." + lm.membername, 1);
            } else {
                string accessor = "msg." + lm.membername;
                unsigned int n;
                for (n = 0; n < lm.dimensions.size() - 1; ++n) {
                    auto& dim = lm.dimensions[n];
                    accessor += "[i" + to_string(n) + "]";
                    if (dim.mode == ZCM_CONST) {
                        emit(n + 1, "for i%d in range(1,%s)", n, dim.size.c_str());
                    } else {
                        emit(n + 1, "for i%d in range(1,msg.%s)", n, dim.size.c_str());
                    }
                }

                // last dimension.
                auto& lastDim = lm.dimensions[lm.dimensions.size() - 1];
                bool lastDimFixedLen = (lastDim.mode == ZCM_CONST);

                if (ZCMGen::isPrimitiveType(lm.type.fullname) &&
                    lm.type.fullname != "string") {
                    emitEncodeListMember(lm, accessor, n + 1, lastDim.size, lastDimFixedLen);
                } else {
                    if (lastDimFixedLen) {
                        emit(n + 1, "for i%d in range(1,%s)", n, lastDim.size.c_str());
                    } else {
                        emit(n + 1, "for i%d in range(1,msg.%s)", n, lastDim.size.c_str());
                    }
                    accessor += "[i" + to_string(n) + "]";
                    emitEncodeSingleMember(lm, accessor, n + 2);
                    emit(n + 1, "end");
                }
                for (int i = n - 1; i >= 0; --i)
                    emit(i + 1, "end");
            }
        }

        emit(0, "end");
        emit(0, "");
    }

    void emitEncode()
    {
        auto* sn = ls.structname.shortname.c_str();

        emit(0, "function ZCM.encode(msg::%s)", sn);
        emit(0, "    buf = IOBuffer()");
        emit(0, "    write(buf, %s(ZCM.getHash(%s)))", hton.c_str(), sn);
        emit(0, "    ZCM._encode_one(msg, buf)");
        emit(0, "    return ZCM._takebuf_array(buf);");
        emit(0, "end");
        emit(0, "");
    }

    void emitDecodeSingleMember(ZCMMember& lm, const string& accessor_,
                                int indent, const string& sfx_)
    {
        auto& tn = lm.type.fullname;
        string mappedTypename = mapTypeName(tn);
        auto& mn = lm.membername;

        auto *accessor = accessor_.c_str();
        auto *sfx = sfx_.c_str();

        if (tn == "string") {
            emit(indent, "%sString(read(buf, %s(reinterpret(UInt32, read(buf, 4))[1])))%s",
                         accessor, ntoh.c_str(), sfx);
        } else if (tn == "byte"    || tn == "boolean" || tn == "int8_t") {
            auto typeSize = ZCMGen::getPrimitiveTypeSize(tn);
            emit(indent, "%sreinterpret(%s, read(buf, %u))[1]%s",
                         accessor, mappedTypename.c_str(), typeSize, sfx);
        } else if (tn == "int16_t" || tn == "int32_t" || tn == "int64_t" ||
                   tn == "float"   || tn == "double") {
            auto typeSize = ZCMGen::getPrimitiveTypeSize(tn);
            emit(indent, "%s%s(reinterpret(%s, read(buf, %u))[1])%s",
                         accessor, ntoh.c_str(), mappedTypename.c_str(), typeSize, sfx);
        } else {
            emit(indent, "%sZCM._decode_one(msg.%s,buf)%s", accessor, mn.c_str(), sfx);
        }
    }

    void emitDecodeListMember(ZCMMember& lm, const string& accessor_, int indent,
                              bool isFirst, const string& len_, bool fixedLen)
    {
        auto& tn = lm.type.fullname;
        string mappedTypename = mapTypeName(tn);
        const char *suffix = isFirst ? "" : ")";
        auto *accessor = accessor_.c_str();
        auto *len = len_.c_str();

        if (tn == "byte" || tn == "boolean" || tn == "int8_t" ) {
            if (fixedLen) {
                emit(indent, "%sreinterpret(%s, read(buf, %d))%s",
                     accessor, mappedTypename.c_str(),
                     atoi(len) * ZCMGen::getPrimitiveTypeSize(tn),
                     suffix);
            } else {
                emit(indent, "%sreinterpret(%s, read(buf, (msg.%s) * %lu))%s",
                     accessor, mappedTypename.c_str(),
                     len, ZCMGen::getPrimitiveTypeSize(tn),
                     suffix);
            }
        } else if (tn == "int16_t" || tn == "int32_t" || tn == "int64_t" ||
                   tn == "float"   || tn == "double") {
            if (fixedLen) {
                emit(indent, "%s%s.(reinterpret(%s, read(buf, %d)))%s",
                     accessor, ntoh.c_str(), mappedTypename.c_str(),
                     atoi(len) * ZCMGen::getPrimitiveTypeSize(tn),
                     suffix);
            } else {
                emit(indent, "%s%s.(reinterpret(%s, read(buf, (msg.%s) * %lu)))%s",
                     accessor, ntoh.c_str(), mappedTypename.c_str(),
                     len, ZCMGen::getPrimitiveTypeSize(tn),
                     suffix);
            }
        } else {
            assert(0);
        }
    }

    void emitDecodeOne()
    {
        auto* sn = ls.structname.shortname.c_str();

        emit(0, "function ZCM._decode_one(::Type{%s}, buf)", sn);
        emit(1,     "msg = %s();", sn);

        for (auto& lm : ls.members) {
            if (lm.dimensions.size() == 0) {
                string accessor = "msg." + lm.membername + " = ";
                emitDecodeSingleMember(lm, accessor.c_str(), 1, "");
            } else {
                string accessor = "msg." + lm.membername;

                // iterate through the dimensions of the member, building up
                // an accessor string, and emitting for loops
                uint n = 0;
                for (n = 0; n < lm.dimensions.size()-1; n++) {
                    auto& dim = lm.dimensions[n];

                    if(n == 0) {
                        emit(1, "%s = []", accessor.c_str());
                    } else {
                        emit(n + 1, "%s.append([])", accessor.c_str());
                    }

                    if (dim.mode == ZCM_CONST) {
                        emit(n + 1, "for i%d in range(1,%s)", n, dim.size.c_str());
                    } else {
                        emit(n + 1, "for i%d in range(1,msg.%s)", n, dim.size.c_str());
                    }

                    if(n > 0 && n < lm.dimensions.size()-1) {
                        accessor += "[i" + to_string(n - 1) + "]";
                    }
                }

                // last dimension.
                auto& lastDim = lm.dimensions[lm.dimensions.size()-1];
                bool lastDimFixedLen = (lastDim.mode == ZCM_CONST);

                if (ZCMGen::isPrimitiveType(lm.type.fullname) &&
                    lm.type.fullname != "string") {
                    // member is a primitive non-string type.  Emit code to
                    // decode a full array in one call to struct.unpack
                    if(n == 0) {
                        accessor += " = ";
                    } else {
                        accessor += ".append(";
                    }

                    emitDecodeListMember(lm, accessor, n + 1, n==0,
                                         lastDim.size, lastDimFixedLen);
                } else {
                    // member is either a string type or an inner ZCM type.  Each
                    // array element must be decoded individually
                    if(n == 0) {
                        emit(1, "%s = []", accessor.c_str());
                    } else {
                        emit(n + 1, "%s.append([])", accessor.c_str());
                        accessor += "[i" + to_string(n-1) + "]";
                    }
                    if (lastDimFixedLen) {
                        emit(n + 1, "for i%d in range(1,%s)", n, lastDim.size.c_str());
                    } else {
                        emit(n + 1, "for i%d in range(1,msg.%s)", n, lastDim.size.c_str());
                    }
                    accessor += ".append(";
                    emitDecodeSingleMember(lm, accessor, n + 4, ")");
                    emit(n + 1, "end");
                }
                for (int i = n - 1; i >= 0; --i)
                    emit(i + 1, "end");
            }
        }
        emit(1, "return msg");
        emit(0, "end");
        emit(0, "");
    }

    void emitDecode()
    {
        auto* sn = ls.structname.shortname.c_str();

        emit(0, "function ZCM.decode(::Type{%s}, data::Vector{UInt8})", sn);
        emit(0, "    buf = IOBuffer(data)");
        emit(0, "    if %s(reinterpret(Int64, read(buf, 8))[1]) != ZCM.getHash(%s)",
                ntoh.c_str(), sn);
        emit(0, "        throw(\"Decode error\")");
        emit(0, "    end");
        emit(0, "    return ZCM._decode_one(%s, buf)", sn);
        emit(0, "end");
        emit(0, "");
    }

    void emitType()
    {
        emitModuleStart();
        emitInstance();
        emitGetHash();
        emitEncodeOne();
        emitEncode();
        emitDecodeOne();
        emitDecode();
        emitModuleEnd();
    }
};

struct JlEmitPack : public Emitter
{
    ZCMGen& zcm;

    JlEmitPack(ZCMGen& zcm, const string& fname):
        Emitter(fname), zcm(zcm) {}

    int emitPackage(const string& packName, vector<ZCMStruct*>& packStructs)
    {
        // create the package directory, if necessary
        vector<string> dirs = StringUtil::split(packName, '.');
        int havePackage = dirs.size() > 0;
        string pdname;
        if (havePackage) {
            pdname = dirs.at(0);
        } else {
            pdname = "";
        }
        auto& ppath = zcm.gopt->getString("ppath");
        string packageDirPrefix = ppath + ((ppath.size() > 0) ? "/" : "");
        string packageDir = packageDirPrefix + pdname + (havePackage ? "/" : "");

        if (packageDir != "") {
            if (!FileUtil::exists(packageDir)) {
                FileUtil::mkdirWithParents(packageDir, 0755);
            }
            if (!FileUtil::dirExists(packageDir)) {
                cerr << "Could not create directory " << packageDir << "\n";
                return -1;
            }
        }

        // write the main package module, if any

        // Keep track of which types were already imported into the module
        unordered_set<string> moduleJlImports;

        // Keep track of which submodules were already defined. Note that this
        // set is sorted because we always have to define foo.bar before we
        // can define foo.bar.baz
        set<string> moduleJlSubmodules;
        moduleJlSubmodules.insert(packName);

        FILE *moduleJlFp = nullptr;
        if (havePackage) {
            // If we are inside a package, then we need to generate a single
            // Julia file containing the top-level module and any submodules

            // For a type foo.bar.baz.t1, we will put the module file in
            // foo.jl
            vector<string> moduleJlFnameParts;
            moduleJlFnameParts.push_back(packageDirPrefix);
            moduleJlFnameParts.push_back(pdname + ".jl");
            string moduleJlFname = StringUtil::join(moduleJlFnameParts, '/');
            if (moduleJlFp) {
                fclose(moduleJlFp);
                moduleJlFp = nullptr;
            }
            if (FileUtil::exists(moduleJlFname)) {
                // If the module exists already, then we need to parse it and
                // extract any existing sub-modules and types contained in the
                // module. We'll include those entries in the final generated
                // module file.
                moduleJlFp = fopen(moduleJlFname.c_str(), "r");
                if (!moduleJlFp) {
                    perror("fopen");
                    return -1;
                }
                // Read chunks of the existing file line by line
                while(!feof(moduleJlFp)) {
                    char buf[4096];
                    memset(buf, 0, sizeof(buf));
                    char *result = fgets(buf, sizeof(buf)-1, moduleJlFp);
                    if (!result) break;

                    auto words = StringUtil::split(StringUtil::strip(buf), ' ');
                    if (words.size() >= 2 && words[0] == "import") {
                        // If this line matches "import foo", then store "foo" in
                        // the set of imports
                        moduleJlImports.insert(words[1]);
                    } else if (words.size() >= 6 &&
                               words[0] == "@eval" &&
                               words[2] == "module" &&
                               words[4] == ";" &&
                               words[5] == "end") {
                        // Otherwise, if the line matches:
                        // @eval foo module bar.baz ; end
                        // then store foo.bar.baz in the submodules set
                        moduleJlSubmodules.insert(words[1] + "." + words[3]);
                    }
                }
                fclose(moduleJlFp);
                moduleJlFp = nullptr;
            }
            // Regardless of whether the module existed, we'll create a new file
            moduleJlFp = fopen(moduleJlFname.c_str(), "w");
            if (!moduleJlFp) {
                perror("fopen");
                return -1;
            }
            fprintf(moduleJlFp, "\"\"\"ZCM package %s.jl file\n"
                     "This file automatically generated by zcm-gen.\n"
                     "DO NOT MODIFY BY HAND!!!!\n"
                     "\"\"\"\n\n"
                     "module %s; end\n\n",
                     pdname.c_str(), pdname.c_str());
            for (auto& submod : moduleJlSubmodules) {
                auto parts = StringUtil::split(submod, '.');
                if (parts.size() >= 2) {
                    // Restore each of the submodules we parsed from the
                    // existing file (if any), and also generate the submodule
                    // corresponding to the current package (if necessary)
                    vector<string> parentParts(parts.begin(), parts.end() - 1);
                    auto parent = StringUtil::join(parentParts, '.');
                    auto module = parts.at(parts.size() - 1);
                    fprintf(moduleJlFp, "@eval %s module %s ; end\n", parent.c_str(), module.c_str());
                }
            }
            // LOAD_PATH controls where Julia looks for files you `import`.
            // We're going to tell Julia that if we're in a package foo.bar.baz,
            // it should first look in the `foo/` folder for any types it imports.
            // unshift!(x, y) in Julia prepends y to the vector x. Its opposite
            // is shift!(x) which removes the first element of x. We'll put that
            // shift!(LOAD_PATH) in a `finally` block to ensure that the
            // LOAD_PATH is restored even if something goes wrong with the
            // imports
            fprintf(moduleJlFp, "\nunshift!(LOAD_PATH, joinpath(@__DIR__, \"%s\"))\n", pdname.c_str());
            fprintf(moduleJlFp, "try\n");
            for (auto& import : moduleJlImports) {
                // Restore each of the existing imports. Each import defines a
                // single ZCM type somewhere in the package or one of its
                // submodule
                fprintf(moduleJlFp, "import %s", import.c_str());
            }
        }

        ////////////////////////////////////////////////////////////
        // STRUCTS
        for (auto *ls_ : packStructs) {
            auto& ls = *ls_;
            string path = packageDir + "_" + ls.structname.nameUnderscore() + ".jl";

            // If we're in a package, then add an appropriate import statement
            // to ensure that this struct is added to the Julia module
            if(moduleJlFp) {
                fprintf(moduleJlFp, "import _%s\n", ls.structname.nameUnderscoreCStr());
            }

            if (!zcm.needsGeneration(ls.zcmfile, path))
                continue;

            EmitJulia E{zcm, ls, path};
            if (!E.good()) return -1;
            E.emitType();
        }

        if(moduleJlFp) {
            // Restore LOAD_PATH no matter what
            fprintf(moduleJlFp, "finally\n");
            fprintf(moduleJlFp, "    shift!(LOAD_PATH)\n");
            fprintf(moduleJlFp, "end\n");
            fclose(moduleJlFp);
            moduleJlFp = nullptr;
        }

        return 0;
    }
};

int emitJulia(ZCMGen& zcm)
{
    // Copied wholesale from EmitPython.cpp
    unordered_map<string, vector<ZCMStruct*> > packages;

    // group the structs by package
    for (auto& ls : zcm.structs)
        packages[ls.structname.package].push_back(&ls);

    for (auto& kv : packages) {
        auto& name = kv.first;
        auto& pack = kv.second;
        int ret = JlEmitPack{zcm, name}.emitPackage(name, pack);
        if (ret != 0) return ret;
    }

    return 0;
}
