#define EXPORT __attribute__((visibility("default"))) extern "C"
#define IMPORT extern "C"
#define PRINT_LIT(lit) puts((char *)lit, sizeof(lit) - 1)
// #define memcpy __builtin_memcpy
// #define memset __builtin_memset
#define HASH(lit) djb_hash((char *)lit)
#define INSERT_LIT(lit, writePos)           \
    *writePos++ = sizeof(lit) - 1;          \
    memcpy(writePos, lit, sizeof(lit) - 1); \
    writePos += sizeof(lit) - 1;

#include "wasm_definitions.h"

struct Token
{
    enum Type
    {
        Symbol,
        Number,
        Identifier,
        CharLit,
        StringLit,
    };

    char *start, *end;
    Token::Type type;
};

struct ParsingMode
{
    enum Mode
    {
        DefiningImportedFunction,
        DefiningExportedFunction,
    };
};

extern u8 __data_end;
extern u8 __heap_base;

IMPORT void puts(char *address, u32 size);
IMPORT void put(u32 character);
IMPORT void putu32(u32 num);
IMPORT void puti32(i32 num);

void print(Token token)
{
    puts(token.start, token.end - token.start);
}

//Clang 10 appears to no longer provide a default implementation of memcpy or memset when targeting WebAssembly
void memcpy(void *destination, const void *source, u32 length)
{
    u8 *dest = (u8 *)destination;
    u8 *src = (u8 *)source;

    for (u32 i = 0; i < length; ++i)
    {
        dest[i] = src[i];
    }
}

char *readPos, *endReadPos;
u8 *writePos;

//limitation of max 64 local vars
u64 varNameHashes[64];
u8 varTypes[64];

u64 globalVarNameHashes[64];
u32 globalVarAddresses[64];
u8 globalVarTypes[64];
u32 globalVarCount;

u8 varStartingIndexes[4] = {0};
u8 varCountByType[4] = {0};

//max 64 total imported and locally defined functions
u64 funcNameHashes[64];
u8 funcSigs[64];
u8 funcCount; //sum of both imported and locally defined

//mapping between type index to encoded function signature
u64 types[32];

u8 WASM_HEADER[] = {
    0x00, 0x61, 0x73, 0x6d, //magic numbers
    0x01, 0x00, 0x00, 0x00, //wasm version
};

/* D. J. Bernstein hash function */
//this version doesn't reply on an end pointer so clang doesn't complain about end pointers of string literals
constexpr u64 djb_hash(char *c)
{
    u64 hash = 5381;
    while (*c)
    {
        hash = 33 * hash ^ (unsigned char)*c++;
    }
    return hash;
}

/* D. J. Bernstein hash function */
constexpr u64 djb_hash(char *start, char *end)
{
    u64 hash = 5381;
    while (start != end)
    {
        hash = 33 * hash ^ (unsigned char)*start++;
    }
    return hash;
}

constexpr u64 djb_hash(Token token) {
    return djb_hash(token.start, token.end);
}

constexpr bool isalpha(char c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

constexpr bool isdigit(char c)
{
    return c >= '0' && c <= '9';
}

constexpr bool isValidLeadingIDChar(char c)
{
    return isalpha(c) || c == '_';
}

constexpr bool isValidNonLeadingIDChar(char c)
{
    return isValidLeadingIDChar(c) || isdigit(c);
}

constexpr i32 stoi(char *start, char *end);
constexpr f32 stof(char *start, char *end);

//scan forward and find the next complete token
Token nextToken(char *p)
{
    while (p < endReadPos && (*p == ' ' || *p == '\n' || *p == '\t'))
    {
        ++p;
    }

    Token token;
    token.start = p;

    if ((*p == '-' && (p[1] == '.' || isdigit(p[1]))) || *p == '.' || isdigit(*p))
    {
        //decimal places separate identifiers, but join numeric literals
        token.type = Token::Number;

        do
        {
            ++p;
        } while (isdigit(*p) || *p == '.' || *p == 'f');
    }
    else if (*p == '\'')
    {
        token.type = Token::CharLit;

        ++p;
        if (*p == '\\')
        {
            ++p;
        }
        p += 2;
    }
    else if (*p == '"')
    {
        token.type = Token::StringLit;

        do
        {
            //skip over escape sequences
            if (*p == '\\')
            {
                ++p;
            }
            ++p;
        } while (*p != '"');

        ++p;
    }
    else if (isValidLeadingIDChar(*p))
    {
        token.type = Token::Identifier;

        do
        {
            ++p;
        } while (isValidNonLeadingIDChar(*p) || *p == ':'); //the : is to handle namespaces
    }
    else
    {
        token.type = Token::Symbol;

        // check for repeating symbols representing two-character symbols
        if ((*p == '-' || *p == '+' || *p == '&' || *p == '|' || *p == '<' || *p == '>') && *p == p[1])
        {
            ++p;
        }

        // -> is a valid two-character symbols
        if (*p == '-' && *p + 1 == '>')
        {
            ++p;
        }

        ++p;
    }

    token.end = p;
    return token;
}

u8 getWasmOpFromOperator(Token token, u8 wasmType)
{
    // TODO finish list, recognize remainder of operators, move this into wasm_definitions.h

    if (wasmType == wasm::type::f32) {
        switch (*token.start)
        {
        case '+':
            return wasm::f32_add;
        case '-':
            return wasm::f32_sub;
        case '*':
            return wasm::f32_mul;
        case '/':
            return wasm::f32_div;
        case '<':
            return wasm::f32_lt;
        case '>':
            return wasm::f32_gt;
        }
    }

    if (wasmType == wasm::type::i32) {
        switch (*token.start)
        {
        case '+':
            return wasm::i32_add;
        case '-':
            return wasm::i32_sub;
        case '*':
            return wasm::i32_mul;
        case '/':
            return wasm::i32_div_s;
        case '<':
            return wasm::i32_lt_s;
        case '>':
            return wasm::i32_gt_s;
        }
    }

    return wasm::unreachable;
}

//TODO, obviously
u8 getWasmLoadInstructionFromType(u8 type) {
    switch (type) {
        case wasm::type::i32:
        return wasm::i32_load;
        case wasm::type::i64:
        return wasm::i64_load;
        case wasm::type::f32:
        return wasm::f32_load;
        case wasm::type::f64:
        return wasm::f64_load;
    }

    return wasm::unreachable;
}

u8 getWasmStoreInstructionFromType(u8 type) {
    switch (type) {
        case wasm::type::i32:
        return wasm::i32_store;
        case wasm::type::i64:
        return wasm::i64_store;
        case wasm::type::f32:
        return wasm::f32_store;
        case wasm::type::f64:
        return wasm::f64_store;
    }

    return wasm::unreachable;
}

u8 getWasmTypeFromCppName(u64 hash)
{
    //for the purposes of this hackathon, assume no unsigned types and well formed programs
    switch (hash)
    {
    case HASH("int"):
    case HASH("i32"):
    case HASH("u32"):
    case HASH("char"):
        return wasm::type::i32;
    case HASH("long"):
    case HASH("i64"):
    case HASH("u64"):
        return wasm::type::i64;
    case HASH("float"):
    case HASH("f32"):
        return wasm::type::f32;
    case HASH("double"):
    case HASH("f64"):
        return wasm::type::f64;
    case HASH("void"):
        return wasm::type::_void;
    default:
        return 0;
    }
}

void writeF32(f32 val)
{
    memcpy(writePos, &val, 4);
    writePos += 4;
}

void writeSectionSize(u8 *sectionSizePtr)
{
    u32 sectionSize = writePos - sectionSizePtr - 2;
    sectionSizePtr[0] = (sectionSize & 0x7F) | 0x80;
    sectionSizePtr[1] = sectionSize >> 7;
}

//readPos must be placed at the first character of the return type for a function definition
void writeFunction();
u8 writeExpression();

u32 getFuncIndex(u64 funcNameHash);
u32 getLocalVarIndex(u64 varNameHash);
u32 getGlobalVarIndex(u64 varNameHash);

u8 writeMetaData();

EXPORT u32 getWasmFromCpp(char *sourceCode, u32 length)
{
    globalVarCount = 0;

    //start placing the compiled output 4 bytes after the source code input
    writePos = (u8 *)(sourceCode + length + 4);

    //begin the output wasm binary with the 8 byte wasm header
    for (int i = 0; i < 8; ++i)
    {
        *writePos++ = WASM_HEADER[i];
    }

    readPos = sourceCode;
    endReadPos = sourceCode + length;

    //detect source code metadata and write the wasm binary up to just before the Code section
    u8 localFuncCount = writeMetaData();

    //reset the read position for the code generation pass
    readPos = sourceCode;

    *writePos++ = wasm::section::Code;
    u8 *codeSectionSize = writePos;
    writePos += 2;
    *writePos++ = localFuncCount;

    //scan the program looking for each function in-order
    bool definingExternalResource = false;
    u32 lhsType = 0;
    char *identifierStart = nullptr;
    char *identifierEnd = nullptr;

    while (readPos < endReadPos)
    {
        Token token = nextToken(readPos);
        readPos = token.end;

        switch (token.type) {
            case Token::Identifier: {
                u64 hash = djb_hash(token);
                u8 wasmType = getWasmTypeFromCppName(hash);
                if (wasmType) {
                    Token next = token;

                    //line starts with a type.  Scan ahead for an '{'
                    while (readPos < endReadPos) {
                        next = nextToken(next.end);

                        if (*next.start == ';') {
                            //either a function header or a global var.  Skip this declaration and move on
                            readPos = next.end;
                            break;
                        }

                        if (*next.start == '{') {
                            //found a function definition
                            readPos = token.start;
                            writeFunction();
                            break;
                        }
                    }
                }
            } break;
            default:
            break;
        }
    }

    // PRINT_LIT("Finished Loop Function\n");

    writeSectionSize(codeSectionSize);

    // PRINT_LIT("Finished Code section\n");

    u32 wasmModuleAddress = (u32)(void *)endReadPos + 4;
    u32 wasmModuleSize = (u32)(void *)writePos - wasmModuleAddress;

    //store the length of the generated binary at the same memory location that
    //the source code was read in, but rounded up to align to 4 bytes
    u32 *wasmModuleSizeWriteAddress = (u32 *)(((u32)sourceCode + 3) & -4);
    *wasmModuleSizeWriteAddress = wasmModuleSize;

    //until multiple-return is finalized, this is the next best solution to return two i32's
    return wasmModuleAddress;
}



/* This function must be called with readPos pointing to the first char of the return type of a function.
writePos must point to the first byte of a function body, where the function body size is encoded */
void writeFunction() {
    //write to this address at the end of the function once the body size is known
    u8* functionBodySize = writePos;
    writePos += 3; // allocate 2**14 bytes for each function body and skip a byte for parameter entries

    //token is assumed to be the return type of this function
    Token token = nextToken(readPos);
    u8 returnType = getWasmTypeFromCppName(djb_hash(token));

    //Skip function name and open paren, then select type of first param
    token = nextToken(token.end);

    token = nextToken(token.end);
    readPos = token.end;

    u32 paramCount = 0;

    //Parse parameters and their types.  Parameters count as local variables
    while (readPos < endReadPos) {
        token = nextToken(readPos);
        readPos = token.end;
        
        if (token.type == Token::Identifier) {
            u64 hash = djb_hash(token);
            u8 wasmType = getWasmTypeFromCppName(hash);
            if (wasmType) {
                Token paramName = nextToken(token.end);
                hash = djb_hash(paramName);
                token = paramName;

                varTypes[paramCount] = wasmType;
                varNameHashes[paramCount++] = hash;

                readPos = paramName.end;
            } else {
                PRINT_LIT("Unable to find wasm type of paramater type \"");
                print(token);
                PRINT_LIT("\"\n");
            }
        } else if (*token.start == ')') {
            break;
        } else if (*token.start != ',') {
            PRINT_LIT("Found non-parameter \"");
            print(token);
            PRINT_LIT("\" in parameter list\n");
        }        
    }

    char* beginningOfFuncBody = readPos;
    i32 scopeDepth;
    u8 varCountByTypeThisScope[64][4];

    //count up the local variables of each type used in each scope so that variables used in
    //different scopes can be assigned to the same local variable
    {
        for (u32 i = 0; i < 4; ++i) {
            varCountByType[i] = 0;
        }

        scopeDepth = -1;
        u8 maxVarCountByType[4] = {0};
        
        while (readPos < endReadPos) {
            token = nextToken(readPos);
            readPos = token.end;

            //TODO support declaring vars in for loops

            if (*token.start == '{') {
                ++scopeDepth;
                for (u32 i = 0; i < 4; ++i) {
                    varCountByTypeThisScope[scopeDepth][i] = 0;
                }
            }
            else if (*token.start == '}') {
                for (u32 i = 0; i < 4; ++i) {
                    //deallocate local vars so the same local var can be reused
                    varCountByType[i] -= varCountByTypeThisScope[scopeDepth][i];
                }
                --scopeDepth;
                if (scopeDepth < 0) {
                    break;
                }
            }
            else if (token.type == Token::Identifier) {
                u64 hash = djb_hash(token);
                u8 wasmType = getWasmTypeFromCppName(hash);
                if (wasmType) {
                    PRINT_LIT("found local var ");
                    print(nextToken(token.end));
                    PRINT_LIT(" of type ");
                    puti32(wasmType);
                    put('\n');

                    u32 i = wasmType & 0b11;
                    ++varCountByTypeThisScope[scopeDepth][i];
                    ++varCountByType[i];

                    if (maxVarCountByType[i] < varCountByType[i]) {
                        maxVarCountByType[i] = varCountByType[i];
                    }
                }
            }
        }

        //encode local variable metadata at the top of the function body
        u8 paramEntryCount = 0;

        int j = paramCount;
        for (u32 i = 0; i < 4; ++i) {
            if (maxVarCountByType[i] > 0) {
                ++paramEntryCount;
                *writePos++ = maxVarCountByType[i]; //# of parameters of the following type
                *writePos++ = wasm::type::f64 | i; //parameter type

                //write out all the local variable types once since they do not change during scope changes
                for (u32 k = 0; k < maxVarCountByType[i]; ++k) {
                    varTypes[j++] = wasm::type::f64 | i;
                }
            }

            
            // PRINT_LIT("WasmType of type ");
            // puti32(i | wasm::type::f64);
            // PRINT_LIT(" has # of local vars: ");
            // puti32(maxVarCountByType[i]);
            // put('\n');
        }

        functionBodySize[2] = paramEntryCount;

        varStartingIndexes[0] = paramCount;
        for (u32 i = 1; i < 4; ++i) {
            varStartingIndexes[i] = varStartingIndexes[i-1] + maxVarCountByType[i-1];
        }
    }

    readPos = beginningOfFuncBody;
    u32 varIndexToAssignTo = -1;
    u32 globalVarIndexToAssignTo = -1;
    u32 funcIndexToCall = -1;
    bool isIfStatement = false;

    u8 lhsType = 0;

    //don't read past the end of the input string in the event of malformed C++
    while (readPos < endReadPos) {
        token = nextToken(readPos);
        readPos = token.end;

        if (*token.start == '{') {
            ++scopeDepth;
            
            for (u32 i = 0; i < 4; ++i) {
                varCountByTypeThisScope[scopeDepth][i] = 0;
            }
        }
        else if (*token.start == '}') {
            for (u32 i = 0; i < 4; ++i) {
                //deallocate local vars so the same local var can be reused
                varCountByType[i] -= varCountByTypeThisScope[scopeDepth][i];
            }
            --scopeDepth;

            if (scopeDepth < 0) {
                break;
            }

            *writePos++ = wasm::end;
        }
        else if (token.type == Token::Identifier) {
            u64 hash = djb_hash(token);
            u32 wasmType = getWasmTypeFromCppName(hash);
            if (wasmType) {
                //if the identifier on the beginning of the line is a type name, then declare
                //a variable of that type with the following identifier as its name/hash
                token = nextToken(readPos);
                readPos = token.end;
                hash = djb_hash(token);

                int i = wasmType & 0b11;
                varIndexToAssignTo = varStartingIndexes[i] + varCountByType[i]++;
                varNameHashes[varIndexToAssignTo] = hash;
            } else if (hash == HASH("if")) {
                // PRINT_LIT("found if statement\n");
                isIfStatement = true;

                //set read position to one char past the open parenthesis
                token = nextToken(readPos);
                readPos = token.end;
                writeExpression();
            }
            else if (hash == HASH("std::cout")) {
                do {
                    token = nextToken(readPos);
                    
                    if (token.end - token.start == 2 && *token.start == '<') {
                        //found << operator, move to next token
                    } else if (*token.start == ';') {
                        //found end of statement
                        break;
                    } else if (token.type == Token::StringLit) {
                        char *c = token.start + 1;

                        while (c != token.end - 1) {
                            if (*c == '\\') {
                                ++c;
                                if (*c == 'n') {
                                    *c = '\n';
                                }
                            }

                            *writePos++ = wasm::i32_const;
                            writePos += wasm::varint(writePos, (u8)*c);
                            u8 printFunc = getFuncIndex(HASH("put"));
                            if (printFunc == (u8)-1) {
                                PRINT_LIT("Failed to find function 'put'\n");
                            } else {
                                *writePos++ = wasm::call;
                                *writePos++ = printFunc;
                            }

                            ++c;
                        }
                    } else if (token.type == Token::CharLit) {
                        u8 c = token.start[2];

                        if (token.start[1] == '\\') {
                            if (c == 'n') {
                                c = '\n';
                            }
                            //TODO support remaining escape sequences
                        }

                        *writePos++ = wasm::i32_const;
                        writePos += wasm::varint(writePos, c);
                        u8 printFunc = getFuncIndex(HASH("put"));
                        if (printFunc == (u8)-1) {
                            PRINT_LIT("Failed to find function 'put'\n");
                        } else {
                            *writePos++ = wasm::call;
                            *writePos++ = printFunc;
                        }
                    } else {
                        u8 wasmType = writeExpression();
                        u8 printFunc = -1;
                        switch(wasmType) {
                            case wasm::type::i32:
                                printFunc = getFuncIndex(HASH("puti32"));
                                break;
                            case wasm::type::f32:
                                printFunc = getFuncIndex(HASH("putf32"));
                                break;
                            case wasm::type::f64:
                                printFunc = getFuncIndex(HASH("putf64"));
                                break;
                        }

                        if (printFunc == (u8)-1) {
                            PRINT_LIT("Failed to find print function for type ");
                            puti32(wasmType);
                            put('\n');
                        } else {
                            *writePos++ = wasm::call;
                            *writePos++ = printFunc;
                        }
                    }
                    
                    readPos = token.end;
                } while (readPos < endReadPos);
            } else {
                funcIndexToCall = getFuncIndex(hash);

                if (funcIndexToCall == -1) {
                    varIndexToAssignTo = getLocalVarIndex(hash);

                    if (varIndexToAssignTo == -1) {
                        globalVarIndexToAssignTo = getGlobalVarIndex(hash);
                        if (globalVarIndexToAssignTo != -1) {
                            *writePos++ = wasm::i32_const;
                            *writePos++ = 0;
                        }
                    }
                }

                if (funcIndexToCall != -1 || varIndexToAssignTo != -1 || globalVarIndexToAssignTo != -1) {
                    //skip past '(' or '='
                    token = nextToken(readPos);
                    readPos = token.end;
                }
            }

            lhsType = writeExpression();
        }
        else if (*token.start == ';') {
            if (funcIndexToCall != -1) {
                u8 returnType = types[funcSigs[funcIndexToCall]] >> 61;
                if (returnType != 4) {
                    lhsType = returnType | wasm::type::f64;
                }

                *writePos++ = wasm::call;
                *writePos++ = funcIndexToCall;
                funcIndexToCall = -1;                
            }

            if (varIndexToAssignTo != -1) {
                *writePos++ = wasm::set_local;
                *writePos++ = varIndexToAssignTo;
                varIndexToAssignTo = -1;
            } else if (globalVarIndexToAssignTo != -1) {
                u8 type = globalVarTypes[globalVarIndexToAssignTo];
                u8 storeInstruction = getWasmStoreInstructionFromType(type);
                u32 address = globalVarAddresses[globalVarIndexToAssignTo];

                *writePos++ = storeInstruction;
                //Alignment: 2 for i32 and f32, 3 for i64 and f64.  Temporary TODO
                *writePos++ = 3 - (type & 1);
                writePos += wasm::varuint(writePos, address);

                globalVarIndexToAssignTo = -1;
            }
            // else {
            //     if (lhsType) {
            //         *writePos++ = wasm::drop;
            //     }
            // }

            lhsType = 0;
        }
        
        if (*token.start == ')' && isIfStatement) {
            *writePos++ = wasm::_if;
            *writePos++ = wasm::type::_void;
            isIfStatement = false;
        }
    }

    //end of function
    u8 flush = getFuncIndex(HASH("flushStdout"));
    if (flush != (u8)-1) {
        *writePos++ = wasm::call;
        *writePos++ = flush;
    }
    *writePos++ = wasm::end;

    //patch in the body size of the function earlier in the output
    writeSectionSize(functionBodySize);
}

u8 writeExpression() {
    u8 queuedOperation = 0;
    u8 lhsType = 0;
    u8 rhsType = 0;

    while (readPos < endReadPos) {
        Token token = nextToken(readPos);
        readPos = token.end;

        if (*token.start == ';' || *token.start == ')' || (token.end - token.start == 2 && *token.start == '<')) {
            readPos = token.start;
            break;
        }

        //assume every token is an identifier, a number, or an operator
        else if (token.type == Token::Identifier) {
            u64 hash = djb_hash(token);
            u32 varIndex = getLocalVarIndex(hash);

            if (varIndex != -1) {
                *writePos++ = wasm::get_local;
                *writePos++ = varIndex;

                if (queuedOperation) {
                    *writePos++ = queuedOperation;
                    queuedOperation = 0;
                }

                lhsType = rhsType;
                rhsType = varTypes[varIndex];
            } else {
                varIndex = getGlobalVarIndex(hash);
                if (varIndex != -1) {
                    u8 type = globalVarTypes[varIndex];
                    u8 loadInstruction = getWasmLoadInstructionFromType(type);
                    u32 address = globalVarAddresses[varIndex];

                    *writePos++ = wasm::i32_const;
                    *writePos++ = 0;
                    *writePos++ = loadInstruction;
                    //Alignment: 2 for i32 and f32, 3 for i64 and f64.  Temporary TODO       
                    *writePos++ = 3 - (type & 1);
                    writePos += wasm::varuint(writePos, address);

                    lhsType = rhsType;
                    rhsType = type;
                }
            }
        }
        
        else if (token.type == Token::Number) {
            bool isFloat = false;
            for (char* c = token.start; c != token.end; ++c) {
                if (*c == '.' || *c == 'f') {
                    isFloat = true;
                }
            }

            if (token.end[-1] == 'f') {
                //make the trailing f at the end of literals optional
                --token.end;
            }

            lhsType = rhsType;

            if (isFloat) {
                *writePos++ = wasm::f32_const;
                f32 result = stof(token.start, token.end);
                writeF32(result);

                rhsType = wasm::type::f32;
            } else {
                *writePos++ = wasm::i32_const;
                i32 result = stoi(token.start, token.end);
                writePos += wasm::varint(writePos, result);

                rhsType = wasm::type::i32;
            }

            if (queuedOperation) {
                *writePos++ = queuedOperation;
                queuedOperation = 0;
            }
        }
        
        else {
            u8 wasmOp = getWasmOpFromOperator(token, rhsType);

            //for now, don't take into account operator precedence
            if (queuedOperation) {
                *writePos++ = queuedOperation;
            }

            queuedOperation = wasmOp;
        }
    }

    if (queuedOperation) {
        *writePos++ = queuedOperation;
        queuedOperation = 0;
    }

    //return the type of the expression
    return rhsType;
}

u32 getFuncIndex(u64 hash) {
    //check all imported and locally defined functions
    for (u32 i = 0; i < funcCount; ++i) {
        if (hash == funcNameHashes[i]) {
            return i;
        }
    }

    return -1;
}

u32 getLocalVarIndex(u64 hash) {
    //check for parameters for a match
    for (u32 i = 0; i < varStartingIndexes[0]; ++i) {
        if (hash == varNameHashes[i]) {
            return i;
        }
    }

    //check each of the 4 types of local vars for matches
    for (u32 i = 0; i < 4; ++i) {
        for (u32 j = 0; j < varCountByType[i]; ++j) {
            u32 varIndex = varStartingIndexes[i] + j;
            if (hash == varNameHashes[varIndex]) {
                return varIndex;
            }
        }
    }

    return -1;
}

u32 getGlobalVarIndex(u64 hash) {
    //check for parameters for a match
    for (u32 i = 0; i < globalVarCount; ++i) {
        if (hash == globalVarNameHashes[i]) {
            return i;
        }
    }

    return -1;
}

constexpr i32 stoi(char *c, char *end) {
    i32 result = 0;
    i32 sign = 1;

    if (*c == '-') {
        sign = -1;
        ++c;
    }

    while (c != end)
    {
        result = result * 10 + (*c++ - '0');
    }

    return result * sign;
}

constexpr f32 stof(char *c, char *end)
{
    //For now, only convert fixed-point float literals to float values
    f32 result = 0.0f;
    f32 sign = 1.0f;

    if (*c == '-')
    {
        sign = -1.0f;
        ++c;
    }

    //calculate portion of value larger than 10
    //NOTE: float literals may begin with a decimal point e.g. ".01f"
    while (c != end && *c != '.')
    {
        result = result * 10.0f + (*c++ - '0');
    }

    if (c != end && *c == '.')
    {
        ++c;

        float scale = 0.1f;
        while (c != end)
        {
            result += scale * (*c++ - '0');
            scale *= 0.1f;
        }
    }

    return result * sign;
}

/* Scan through the entire source code and write down the imported functions, exported functions.
function signatures, and global variables w/ initial values.

Anything that appears outside of a function definition is scanned during this pass and written to the binary.  */

struct FuncHeader
{
    char *nameStart;
    u16 nameLength;
    u8 typeIndex;
    bool isExported;
};

u8 writeMetaData()
{
    /* keep note of all function signatures (wasm types) used in a given source code.
    signatures are encoded as follows to allow O(1) equality checks between two fignatures and efficient encoding and decoding

    00000000000000000000000000000000000000000000000000000000 11111 000
    56 bits to encode 28 paramaters, 2 bits per parameter
    5 bits to encode number of paramaters (2**5 = 32)
    3 bits to encode return type

    void = 4
    i32 = 3
    i64 = 2
    f32 = 1
    f64 = 0

    f64 - i32 have numeric values 124 - 127, so mapping involves a check for void or subtraction by 124
    */
    u8 typeCount = 0;

    //store the locations in memory that define the name of the functions, the length of the name, and the corresponding type
    FuncHeader importedFuncs[32];
    u8 importedFuncCount = 0;

    //record the function signatures of each function defined inside the wasm module, in order
    //assuming no more than 127 unique indexes will be used in a single program
    FuncHeader localFuncs[32];
    u8 localFuncCount = 0;

    bool definingExternalResource = false;
    u32 lhsType = 0;
    char *identifierStart = nullptr;
    char *identifierEnd = nullptr;

    while (readPos < endReadPos)
    {
        Token token = nextToken(readPos);

        switch (token.type)
        {
        case Token::Symbol:
        {
            if (lhsType != 0 && identifierStart != nullptr)
            {
                if (token.start[0] == ';')
                {
                    definingExternalResource = false;
                    
                    u32 address = 0;
                    if (globalVarCount > 0) {
                        //for now, align everything on 8 byte boundaries, TODO
                        address = globalVarAddresses[globalVarCount - 1] + 4;
                        if ((lhsType & 0) == 0) {
                            //i64 or f64
                            address += 4;
                        }
                    }

                    globalVarAddresses[globalVarCount] = address;
                    globalVarNameHashes[globalVarCount] = djb_hash(identifierStart, identifierEnd);
                    globalVarTypes[globalVarCount] = lhsType;

                    ++globalVarCount;
                }
                else if (token.start[0] == '(')
                {
                    //this looks like the start of a function header
                    //scan its parameters to determine the function signature
                    u64 type = 0;
                    u8 paramCount = 0;

                    //encode earlier paramater types at higher addresses to avoid variable bit shifts
                    do
                    {
                        token = nextToken(token.end);
                        if (token.type == Token::Identifier)
                        {
                            u64 hash = djb_hash(token);
                            u32 wasmType = getWasmTypeFromCppName(hash);
                            if (wasmType)
                            {
                                type = (type << 2) | (wasmType & 0b11);
                                ++paramCount;
                            }
                        }
                    } while (*token.start != ')');

                    //assign a value of 0-4 to the highest 3 bits of type to indicate return type
                    //encode the number of parameters in the 5 bits immediately below that
                    type |= ((u64)paramCount << 56) | ((u64)(lhsType - wasm::type::f64) << 61);

                    //now check if this func signature has been used before.  Either grab a reference to the
                    //previously used func index or generate a new unique func sig
                    u8 typeIndex = 255;
                    for (u32 i = 0; i < typeCount; ++i)
                    {
                        if (type == types[i])
                        {
                            typeIndex = i;
                        }
                    }

                    //this func sig wasn't previously defined, so define it
                    if (typeIndex == 255)
                    {
                        typeIndex = typeCount++;
                        types[typeIndex] = type;
                    }

                    FuncHeader func = {
                        identifierStart,
                        (u16)(identifierEnd - identifierStart),
                        typeIndex,
                        !definingExternalResource //assume local functions are always exported
                    };

                    if (definingExternalResource) {
                        definingExternalResource = false;
                        importedFuncs[importedFuncCount++] = func;
                    } else {
                        localFuncs[localFuncCount++] = func;
                    }

                    identifierStart = nullptr;
                    identifierEnd = nullptr;
                    lhsType = 0;

                    //the next symbol is either ';' or '{'.  Ignore bodies of functions in this pass
                    Token next = nextToken(token.end);
                    if (next.type == Token::Symbol)
                    {
                        if (next.start[0] == ';')
                        {
                            token = next;
                        }
                        else if (next.start[0] == '{')
                        {
                            //match brackets to skip whole function body
                            int bracketDepth = 1;
                            do
                            {
                                next = nextToken(next.end);
                                if (next.type == Token::Symbol)
                                {
                                    if (next.start[0] == '{')
                                    {
                                        ++bracketDepth;
                                    }
                                    else if (next.start[0] == '}')
                                    {
                                        --bracketDepth;
                                    }
                                }
                            } while (bracketDepth > 0);

                            token = next;
                        }
                        else
                        {
                            PRINT_LIT("Unrecognized symbol after function paramaters: ");
                            print(next);
                            put('\n');
                        }
                    }
                    else
                    {
                        PRINT_LIT("Expected symbol after function paramaters. Found: \"");
                        print(next);
                        PRINT_LIT("\"\n");
                    }
                }
            }
        }
        break;

        case Token::Number:
        {
            //numbers are not expected in the global scope at this time
            //TODO come back once global variables are supported again
        }
        break;

        case Token::Identifier:
        {
            u64 hash = djb_hash(token);
            u32 wasmType = getWasmTypeFromCppName(hash);
            if (wasmType)
            {
                lhsType = wasmType;
            }
            else
                switch (hash)
                {
                //compare the identifier with known reserved keywords that can appear in the global scope
                case HASH("extern"):
                {
                    definingExternalResource = true;

                    //check the next token to see if there is a whole `extern "C"`
                    Token next = nextToken(token.end);
                    if (next.type == Token::StringLit && next.end - next.start == 3 && next.start[1] == 'C')
                    {
                        token = next;
                    }
                }
                break;

                default:
                {
                    //the identifier is neither a type name nor a keyword, so it must be a variable name or function name
                    identifierStart = token.start;
                    identifierEnd = token.end;
                }
                break;
                }
        }
        break;

        case Token::CharLit:
        {
            // global vars not supported at the moment
        }
        break;

        case Token::StringLit:
        {
        }
        break;
        }

        readPos = token.end;
    }

    //all information necessary to populate the Type, Import, Function, Global, and Export sections should be known by this point

    u8 *sectionSizePtr;

    *writePos++ = wasm::section::Type;
    sectionSizePtr = writePos; //# of bytes that belong to this section.  Allocate space up to 16256 bytes
    writePos += 2;
    *writePos++ = typeCount;

    for (u32 i = 0; i < typeCount; ++i)
    {
        u64 encodedType = types[i];
        u32 paramCount = (encodedType >> 56) & 0b11111;
        u32 returnType = encodedType >> 61;

        *writePos++ = wasm::type::func;
        *writePos++ = paramCount;

        //params are encoded in reverse order, so place them in reverse order
        for (i32 j = paramCount - 1; j >= 0; --j)
        {
            writePos[j] = wasm::type::f64 | (encodedType & 0b11);
            encodedType >>= 2;
        }
        writePos += paramCount;

        *writePos++ = returnType != 4;
        if (returnType != 4)
        {
            *writePos++ = returnType + wasm::type::f64;
        }
    }

    writeSectionSize(sectionSizePtr);

    *writePos++ = wasm::section::Import;
    sectionSizePtr = writePos;
    writePos += 2;
    *writePos++ = importedFuncCount;

    for (u32 i = 0; i < importedFuncCount; ++i)
    {
        INSERT_LIT("env", writePos);
        *writePos++ = importedFuncs[i].nameLength; //assuming name lengths stay below 127 chars
        memcpy(writePos, importedFuncs[i].nameStart, importedFuncs[i].nameLength);
        writePos += importedFuncs[i].nameLength;
        *writePos++ = wasm::external::Function;
        *writePos++ = importedFuncs[i].typeIndex;
    }

    writeSectionSize(sectionSizePtr);

    *writePos++ = wasm::section::Function;
    sectionSizePtr = writePos;
    writePos += 2;
    *writePos++ = localFuncCount;

    for (u32 i = 0; i < localFuncCount; ++i) {
        *writePos++ = localFuncs[i].typeIndex;
    }

    writeSectionSize(sectionSizePtr);

    *writePos++ = wasm::section::Memory;
    *writePos++ = 4; //# of bytes that belong to this section
    *writePos++ = 1; //one memory defined
    *writePos++ = 1; //memory is limited
    *writePos++ = 1; //initial one page
    *writePos++ = 1; //max one page

    /*  Come back to this section once a stack is implemented.  The only global var will be the stack pointer
    *writePos++ = wasm::section::Global;
    *writePos++ = 1; //# of bytes belong to this section.
    *writePos++ = 0; //# of global variables defined */

    *writePos++ = wasm::section::Export;
    sectionSizePtr = writePos;
    writePos += 3;

    u8 exportedFuncCount = 0;
    for (u32 i = 0; i < localFuncCount; ++i)
    {
        FuncHeader func = localFuncs[i];
        if (func.isExported) {
            ++exportedFuncCount;

            *writePos++ = func.nameLength; //assuming name lengths stay below 127 chars
            memcpy(writePos, func.nameStart, func.nameLength);
            writePos += func.nameLength;
            *writePos++ = wasm::external::Function;
            *writePos++ = i + importedFuncCount; //local function indexed start after the last imported function index
        }
    }

    sectionSizePtr[2] = exportedFuncCount;
    writeSectionSize(sectionSizePtr);


    //when calling functions, the distinction between imported and locally defined functions is irrelavant.
    //Therefore they are stored together here.
    funcCount = importedFuncCount + localFuncCount;

    for (u32 i = 0; i < importedFuncCount; ++i) {
        FuncHeader func = importedFuncs[i];
        funcNameHashes[i] = djb_hash(func.nameStart, func.nameStart + func.nameLength);
        funcSigs[i] = func.typeIndex;
    }

    for (u32 i = importedFuncCount; i < funcCount; ++i) {
        FuncHeader func = localFuncs[i - importedFuncCount];
        funcNameHashes[i] = djb_hash(func.nameStart, func.nameStart + func.nameLength);
        funcSigs[i] = func.typeIndex;
    }

    //this is the number that is recorded in the Code section, so return it
    return localFuncCount;
}