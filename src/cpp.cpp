#define EXPORT __attribute__((visibility("default"))) extern "C"
#define IMPORT extern "C"
#define PRINT_LIT(lit) puts((char *)lit, sizeof(lit) - 1)
#define memcpy __builtin_memcpy
#define memset __builtin_memset
#define HASH(lit) getIdentifierHash((char *)lit, sizeof(lit) - 1)
#define INSERT_LIT(lit, writePos) *writePos++ = sizeof(lit) - 1; memcpy(writePos, lit, sizeof(lit) - 1); writePos += sizeof(lit) - 1;

#include "wasm_definitions.h"

extern u8 __data_end;
extern u8 __heap_base;

char *readPos, *endReadPos;
u8 *writePos;

//limitation of max 32 global and local vars combined.  TODO allow unlimited
//local var metadata is located immediately after the last global variable metadata
u32 varHashes[32] = {0};
u8 varTypes[32] = {0};
u32 globalVarCount = 0;


u8 WASM_HEADER[] = {
    0x00, 0x61, 0x73, 0x6d, //magic numbers
    0x01, 0x00, 0x00, 0x00, //wasm version
};

IMPORT void puts(char *address, u32 size);
IMPORT void put(char address);
IMPORT void putbool(bool value);
IMPORT void putu32(u32 num);
IMPORT void putnum(i32 num);

//a better hashing algorithm would reduce collisions between identifiers
//TODO
constexpr u32 getIdentifierHash(char* str, int length) {
    u32 hash = 0;
    for (int i = 0; i < length; ++i) {
        hash += (hash << 2) + str[i];
    }

    return hash;
}

u8* insertF32(u8* writePos, f32 val) {
    memcpy(writePos, &val, 4);
    return writePos + 4;
}

constexpr bool isalpha(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

constexpr bool isdigit(char c) {
    return c >= '0' && c <= '9';
}

constexpr bool isValidLeadingIDChar(char c) {
    return isalpha(c) || c == '_';
}

constexpr bool isValidNonLeadingIDChar(char c) {
    return isValidLeadingIDChar(c) || isdigit(c);
}

constexpr float stof(char* c, u32 length);

char* findNextIdentifier(char *p) {
    //find where identifier begins
    while (!isValidLeadingIDChar(*p)) {
        ++p;
    };

    return p;
}

//assume p points to a the first character of a valid token (identifier or numeric literal)
char* findTokenEnd(char* p) {
    //decimal places separate identifiers, but join numeric literals
    if (*p == '-' || *p == '.' || isdigit(*p)) {
        do {
            ++p;
        } while (isdigit(*p) || *p == '.' || *p == 'f');
    } else if (isValidLeadingIDChar(*p)) {
        do {
            ++p;
        } while (isValidNonLeadingIDChar(*p) || *p == ':'); //the : is temporary
    } else {
        //assume single character symbols like . and ,
        ++p;
    }

    return p;
}

char* findNextIdentifierOrNumericLiteral(char* p) {
    //find where identifier begins
    while (p < endReadPos && !isValidNonLeadingIDChar(*p)) {
        ++p;
    };

    return p;
}

char* findNextOperator(char* p) {
    //find where identifier begins
    while (p < endReadPos && *readPos != '+' && *readPos != '-' && *readPos != '*' && *readPos != '/') {
        ++readPos;
    };

    return p;
}

char* findNextToken(char* p) {
    while (p < endReadPos && (*p == ' ' || *p == '\n')) {
        ++p;
    }

    return p;
}

u8 getWasmOpFromOperator(char c) {
    //for the purposes of this hackathon, all types are assumed floats
    switch (c) {
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
        default:
            return wasm::unreachable;
    }
}

u8 getWasmTypeFromCppName(u32 hash) {
    //for the purposes of this hackathon, assume no unsigned types and well formed programs
    switch(hash) {
        case HASH("int"):
           return wasm::type::i32;
        case HASH("long"):
            return wasm::type::i64;
        case HASH("float"):
            return wasm::type::f32;
        case HASH("double"):
            return wasm::type::f64;
        default:
            return wasm::type::_void;
    }
}

//readPos must be placed on the '(' character containing the func parameters
void compileAndInsertFunction();

//readPos must be placed on the first character of a identifier or numeric literal
void compileAndInsertIdentifierOrNumericLiteral(u32 totalVarCount);

//readPos must be placed after the open parenthesis of a function call or after an equal sign
//white space doesn't matter
void compileExpression(u32 totalVarCount);

EXPORT u32 getWasmFromCpp(char *sourceCode, u32 length)
{
    //reset the counter in case this module is reused.
    globalVarCount = 0;

    //start placing the compiled writePosput immediately after the input
    writePos = (u8*) (sourceCode + length);

    //begin the writePosputted program with the 8 byte wasm header
    for (int i = 0; i < 8; ++i) {
        *writePos++ = WASM_HEADER[i];
    }

    *writePos++ = wasm::section::Type;
    *writePos++ = 23; //# of bytes that belong to this section
    *writePos++ = 5; //four func headers defined

    *writePos++ = wasm::type::func;
    *writePos++ = 0;
    *writePos++ = 0;

    *writePos++ = wasm::type::func;
    *writePos++ = 1;
    *writePos++ = wasm::type::f32;
    *writePos++ = 0;

    *writePos++ = wasm::type::func;
    *writePos++ = 2;
    *writePos++ = wasm::type::f32;
    *writePos++ = wasm::type::f32;
    *writePos++ = 0;
    
    *writePos++ = wasm::type::func;
    *writePos++ = 3;
    *writePos++ = wasm::type::f32;
    *writePos++ = wasm::type::f32;
    *writePos++ = wasm::type::f32;
    *writePos++ = 0;
    
    *writePos++ = wasm::type::func;
    *writePos++ = 1;
    *writePos++ = wasm::type::i32;
    *writePos++ = 0;
    // PRINT_LIT("Finished Type section\n");


    *writePos++ = wasm::section::Import;
    u8 *importSectionSize = writePos;
    *writePos++ = 0x80; //bytes belong to this section (patched further down)
    *writePos++ = 3; //# functions to import

    INSERT_LIT("env", writePos);
    INSERT_LIT("drawCircle", writePos);
    *writePos++ = wasm::external::Function;
    *writePos++ = 3; //use the fourth function signature: (f32, f32, f32) => (void)

    INSERT_LIT("env", writePos);
    INSERT_LIT("putf32", writePos);
    *writePos++ = wasm::external::Function;
    *writePos++ = 1; //use the second function signature: (f32) => (void)

    INSERT_LIT("env", writePos);
    INSERT_LIT("put", writePos);
    *writePos++ = wasm::external::Function;
    *writePos++ = 4; //use the second function signature: (i32) => (void)

    *importSectionSize = writePos - importSectionSize - 1;
    // PRINT_LIT("Finished Import section\n");


    *writePos++ = wasm::section::Function;
    *writePos++ = 3; //# of bytes that belong to this section
    *writePos++ = 2; //two functions defined inside this module
    *writePos++ = 0; //first uses () -> (void)
    *writePos++ = 2; //second uses (f32, f32) -> (void)
    // PRINT_LIT("Finished Function section\n");


    *writePos++ = wasm::section::Memory;
    *writePos++ = 4; //# of bytes that belong to this section
    *writePos++ = 1; //one memory defined
    *writePos++ = 1; //memory is limited
    *writePos++ = 1; //initial one page
    *writePos++ = 1; //max one page
    // PRINT_LIT("Finished Memory section\n");


    *writePos++ = wasm::section::Global;
    u8 *globalSectionSize = writePos;
    *writePos++ = 0; //# of bytes belong to this section.  This'll be patched further down the code
    *writePos++ = 0; //# of global variables defined

    //scan through the program looking for globals
    readPos = sourceCode;
    endReadPos = sourceCode + length;

    while (readPos < endReadPos) {
        if (isValidLeadingIDChar(*readPos)) {
            char* endOfToken = findTokenEnd(readPos);
            u32 tokenLen = endOfToken - readPos;
            u32 hash = getIdentifierHash(readPos, tokenLen);

            u8 globalVarType = getWasmTypeFromCppName(hash);
            
            readPos = findNextIdentifier(endOfToken);
            endOfToken = findTokenEnd(readPos);
            tokenLen = endOfToken - readPos;
            hash = getIdentifierHash(readPos, tokenLen);

            //look for the two known function names rather than properly look to see if the identifier is a function name
            //TODO do this properly
            if (hash == HASH("main") || hash == HASH("update")) {
                //skip through the the function body
                int scopeDepth = 0;
                while (true) {
                    if (*readPos == '{') {
                        ++scopeDepth;
                    }
                    if (*readPos == '}') {
                        --scopeDepth;
                        if (scopeDepth == 0) {
                            break;
                        }
                    }

                    ++readPos;
                }
            } else {
                varHashes[globalVarCount] = hash;
                varTypes[globalVarCount] = globalVarType;
                ++globalVarCount;
                *writePos++ = globalVarType;
                *writePos++ = 1; //is mutable

                //for now ignore global variable initial values as specified by the user
                switch (globalVarType) {
                    case wasm::type::i32:
                    *writePos++ = wasm::i32_const;
                    *writePos++ = 0;
                    break;

                    case wasm::type::i64:
                    *writePos++ = wasm::i64_const;
                    *writePos++ = 0;
                    break;

                    case wasm::type::f32:
                    *writePos++ = wasm::f32_const;
                    memset(writePos, 0, 4);
                    writePos += 4;
                    break;

                    case wasm::type::f64:
                    *writePos++ = wasm::f64_const;
                    memset(writePos, 0, 8);
                    writePos += 8;
                    break;
                }
                *writePos++ = wasm::end;

                PRINT_LIT("Global \"");
                puts(readPos, tokenLen);
                PRINT_LIT("\"\n");

                //assume only one global var per line for now  TODO
                while (*readPos != '\n') {
                    ++readPos;
                };
            }
        }

        ++readPos;
    }

    //update byte size of global section (assume it fits within 127 bytes)
    globalSectionSize[0] = writePos - globalSectionSize - 1;
    globalSectionSize[1] = globalVarCount;

    //reset the read position so the code generation pass can run later
    readPos = sourceCode;

    // PRINT_LIT("Finished Global section\n");


    *writePos++ = wasm::section::Export;
    u8* exportSectionSize = writePos; //# of bytes that belong to this section
    writePos += 2;
    *writePos++ = 2; //two functions are exported

    INSERT_LIT("main", writePos);
    *writePos++ = wasm::external::Function;
    *writePos++ = 3; //index of function

    INSERT_LIT("update", writePos);
    *writePos++ = wasm::external::Function;
    *writePos++ = 4; //index of function
    // PRINT_LIT("Finished Export section\n");

    u32 size = writePos - exportSectionSize - 2;
    exportSectionSize[0] = (size & 0x7F) | 0x80;
    exportSectionSize[1] = size >> 7;

    *writePos++ = wasm::section::Code;
    u8 *codeSectionSize = writePos;
    *writePos++ = 0x80; //# of bytes (LO)
    *writePos++ = 0x00; //# of bytes (HI)
    *writePos++ = 2; //two functions are defined

    //scan the program looking for each function in-order

    //TODO re-write this function scanner and the global variable scanner to use
    //the findToken() function so I stop repeating code and improve correctness.
    
    //for now, assume an open parenthesis marks the beginning of a function
    while (readPos < endReadPos && (*readPos != '(' || readPos[-1] != 'n' || readPos[-2] != 'i' || readPos[-3] != 'a' || readPos[-4] != 'm')) {
        ++readPos;
    }

    compileAndInsertFunction();

    //I know that there are exactly two functions declared
    //for now just match "update(" and "main("
    while (readPos < endReadPos && (*readPos != '(' || readPos[-1] != 'e' || readPos[-2] != 't' || readPos[-3] != 'a' || readPos[-4] != 'd' || readPos[-5] != 'p' || readPos[-6] != 'u')) {
        ++readPos;
    }

    // PRINT_LIT("Before Loop Function\n");
    compileAndInsertFunction();
    // PRINT_LIT("Finished Loop Function\n");

    size = writePos - codeSectionSize - 2;
    codeSectionSize[0] = (size & 0x7F) | 0x80;
    codeSectionSize[1] = size >> 7;

    // *writePos++ = 2; //size
    // *writePos++ = 0; //no local vars
    // *writePos++ = wasm::end;

    // *writePos++ = 13; //size
    // *writePos++ = 0; //no local vars
    // *writePos++ = wasm::get_local;
    // *writePos++ = 0;
    // *writePos++ = wasm::get_local;
    // *writePos++ = 0;
    // *writePos++ = wasm::f32_const;
    // writePos = insertF32(writePos, 0.15f);
    // *writePos++ = wasm::call;
    // *writePos++ = 0; //drawCircle
    // *writePos++ = wasm::end;

    // PRINT_LIT("Finished Code section\n");


    u32 wasmModuleAddress = (u32)(void*)endReadPos;
    u32 wasmModuleSize = (u32)(void*)writePos - wasmModuleAddress;

    //Wasm only supports one return type.  JavaScript doesn't support i64.
    //For now, limit return address and lengths to 16 bits and pack them together.
    return (wasmModuleAddress << 16) | wasmModuleSize;
}



void compileAndInsertFunction() {
    //write to this address at the end of the function once the body size is known
    u8* functionBodySize = writePos;
    *writePos++ = 0x80; //# of bytes (LO)
    *writePos++ = 0x00; //# of bytes (HI)

    u32 totalVarCount = globalVarCount;

    //Parse parameters and their types.  Parameters count as local variables
    while (readPos < endReadPos && *readPos != ')') {
        if (isValidLeadingIDChar(*readPos)) {
            //assume the parameter list is filled with chains of type identifiers and parameter names
            char* endOfToken = findTokenEnd(readPos);
            u32 tokenLen = endOfToken - readPos;
            u32 hash = getIdentifierHash(readPos, tokenLen);

            u32 paramType = getWasmTypeFromCppName(hash);

            //now extract parameter name
            readPos = findNextIdentifier(endOfToken);
            endOfToken = findTokenEnd(readPos);
            tokenLen = endOfToken - readPos;
            hash = getIdentifierHash(readPos, tokenLen);

            // PRINT_LIT("Parameter \"");
            // puts(readPos, tokenLen);
            // PRINT_LIT("\" index: ");
            // putnum(totalVarCount);
            // put('\n');

            varTypes[totalVarCount] = paramType;
            varHashes[totalVarCount] = hash;
            ++totalVarCount;

            readPos = endOfToken;
        } else {
            ++readPos;
        }
    }

    char* beginningOfFuncBody = readPos;
    u32 varIndexToAssignTo = -1;
    u32 funcIndexToCall = -1;
    bool isIfStatement = false;
    u32 scopeDepth = 0;

    //parse the function twice.  Once to find all local variables and again to generate code
    for (int pass = 0; pass < 2; ++pass) {

        //don't read past the end of the input string in the event of malformed C++
        while (readPos < endReadPos) {
            if (*readPos == '{') {
                ++scopeDepth;
            }

            if (*readPos == '}') {
                --scopeDepth;
                *writePos++ = wasm::end;

                if (scopeDepth == 0) {
                    break;
                }
            }

            char* endOfToken = findTokenEnd(readPos);
            u32 tokenLen = endOfToken - readPos;
            u32 hash = getIdentifierHash(readPos, tokenLen);

            if (isValidLeadingIDChar(*readPos)) {
                u32 wasmType = getWasmTypeFromCppName(hash);
                if (wasmType != wasm::type::_void) {
                    //if the identifier on the beginning of the line is a type name, then declare
                    //a variable of that type with the following identifier as its name/hash
                    readPos = findNextIdentifier(endOfToken);
                    endOfToken = findTokenEnd(readPos);
                    tokenLen = endOfToken - readPos;
                    hash = getIdentifierHash(readPos, tokenLen);

                    varIndexToAssignTo = totalVarCount;

                    if (pass == 0) {
                        PRINT_LIT("Local \"");
                        puts(readPos, tokenLen);
                        PRINT_LIT("\" index: ");
                        putnum(totalVarCount);
                        PRINT_LIT("\" type: ");
                        putnum(wasmType);
                        put('\n');

                        varTypes[totalVarCount] = wasmType;
                        varHashes[totalVarCount] = hash;
                        ++totalVarCount;
                    }
                } else if (pass == 1) {
                    //if the identifier at the beginning of the line isn't a type name,
                    //then check if it is instead a local or global variable

                    if (hash == HASH("if")) {
                        // PRINT_LIT("found if statement\n");
                        isIfStatement = true;
                        readPos = endOfToken;

                        //set read position to one char past the open parenthesis
                        readPos = findNextToken(readPos) + 1;

                        compileExpression(totalVarCount);
                    }
                    else if (hash == HASH("drawCircle")) {
                        funcIndexToCall = 0;

                        //place cursor one char past open paren of this function call
                        while (*readPos++ != '(');
                    }
                    else if (hash == HASH("std::cout")) {
                        //this functionality is very hard coded at the moment.  It will
                        //likely only work with literals and numbers.
                        readPos = endOfToken;

                        while (readPos < endReadPos && *readPos != ';') {
                            if (*readPos == '"') {
                                ++readPos;

                                //translate the string into individual put() calls for now
                                while (readPos < endReadPos && *readPos != '"') {
                                    *writePos++ = wasm::i32_const;
                                    if (*readPos == '\\' && readPos[1] == 'n') {
                                        *writePos++ = '\n';
                                        ++readPos;
                                    } else {
                                        *writePos++ = (u8)*readPos;
                                    }

                                    *writePos++ = wasm::call;
                                    *writePos++ = 2; //put()

                                    ++readPos;
                                }
                            }

                            ++readPos;
                        }
                    } else {
                        //I am aware that this is an O(n) search.  The dataset is small,
                        //so a better algorithm isn't needed yet

                        for (int i = 0; i < totalVarCount; ++i) {
                            if (hash == varHashes[i]) {
                                varIndexToAssignTo = i;
                            }
                        }

                        //place cursor one char past assignment operator
                        while (*readPos++ != '=');
                    }

                }

                if (pass == 0) {
                    //skip the rest of the line.  Assuming only one var declaration per statement
                    while (*readPos++ != ';');
                } else {
                    // PRINT_LIT("Before compileExpression()\n");
                    compileExpression(totalVarCount);
                    // PRINT_LIT("After compileExpression()\n");
                }
            }

            if (*readPos == ';') {
                if (varIndexToAssignTo != -1) {
                    if (varIndexToAssignTo < globalVarCount) {
                        *writePos++ = wasm::set_global;
                        *writePos++ = varIndexToAssignTo;
                    } else {
                        *writePos++ = wasm::set_local;
                        *writePos++ = varIndexToAssignTo - globalVarCount;
                    }

                    varIndexToAssignTo = -1;
                } else if (funcIndexToCall != -1) {
                    *writePos++ = wasm::call;
                    *writePos++ = funcIndexToCall;
                    funcIndexToCall = -1;
                }
            }

            if (*readPos == ')' && isIfStatement) {
                *writePos++ = wasm::_if;
                *writePos++ = wasm::type::_void;
                isIfStatement = false;
            }

            ++readPos;
        }

        //encode local variable metadata at the top of the function body
        if (pass == 0) {
            functionBodySize[2] = totalVarCount - globalVarCount;

            //at the moment, making no effort to collapse repeating parameter types
            for (int i = globalVarCount; i < totalVarCount; ++i) {
                *writePos++ = 1; //one parameter of the following type
                *writePos++ = varTypes[i]; //parameter type
            }

            //restore the readPos so the code generation pass can run
            readPos = beginningOfFuncBody;
        }
    }

    //patch in the body size of the function earlier in the output
    u32 size = writePos - functionBodySize - 2;
    functionBodySize[0] = (size & 0x7F) | 0x80;
    functionBodySize[1] = size >> 7;
}


void compileExpression(u32 totalVarCount) {
    u8 queuedOperation = 0;

    readPos = findNextToken(readPos);

    while (readPos < endReadPos && *readPos != ';' && *readPos != ')') {
        char* endOfToken = findTokenEnd(readPos);
        u32 tokenLen = endOfToken - readPos;

        // PRINT_LIT("Token \"");
        // puts(readPos, tokenLen);
        // PRINT_LIT("\" at ");
        // putnum((u32)(void*)readPos);
        // put('\n');

        //assume every token is an identifier, a number, or an operator
        if (isValidLeadingIDChar(*readPos)) {
            u32 hash = getIdentifierHash(readPos, tokenLen);

            for (int i = 0; i < totalVarCount; ++i) {
                if (hash == varHashes[i]) {
                    if (i < globalVarCount) {
                        *writePos++ = wasm::get_global;
                        *writePos++ = i;
                    } else {
                        *writePos++ = wasm::get_local;
                        *writePos++ = i - globalVarCount;

                    }

                    if (queuedOperation) {
                        *writePos++ = queuedOperation;
                        queuedOperation = 0;
                    }
                }
            }
        } else if ((readPos[0] == '-' && (isdigit(readPos[1]) || readPos[1] == '.')) || isdigit(*readPos) || *readPos == '.') {
            //TODO for now assuming all numeric literals are floating point literals
            if (readPos[tokenLen - 1] == 'f') {
                //make the trailing f at the end of literals optional
                --tokenLen;
            }

            f32 result = stof(readPos, tokenLen);
            readPos = endOfToken;

            *writePos++ = wasm::f32_const;
            insertF32(writePos, result);
            writePos += 4;

            if (queuedOperation) {
                *writePos++ = queuedOperation;
                queuedOperation = 0;
            }
        } else {
            //assume 1 character operators for now (e.g. no <<, >>, &&, or ||)
            u8 wasmOp = getWasmOpFromOperator(*readPos++);

            //for now, don't take into account operator precedence
            if (queuedOperation) {
                *writePos++ = queuedOperation;
            }

            queuedOperation = wasmOp;
        }

        readPos = findNextToken(endOfToken);
    }
}

constexpr float stof(char* c, u32 length) {
    //For now, only convert fixed-point float literals to float values
    float result = 0.0f;
    float sign = 1.0f;

    if (*c == '-') {
        sign = -1.0f;
        ++c;
        --length;
    }

    //calculate portion of value larger than 10
    //NOTE: float literals may begin with a decimal point e.g. ".01f"
    while (length && *c != '.') {
        result = result * 10.0f + (*c++ - '0');
        --length;
    }

    if (length && *c == '.') {
        ++c;
        --length;

        float scale = 0.1f;
        while (length) {
            result += scale * (*c++ - '0');
            scale *= 0.1f;
            --length;
        }
    }

    return result * sign;
}