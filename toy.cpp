#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

///====Lexer====

// the Lexer returns tokens [0-255] if it is an unknown character, otherwise one of 
// these for known things
enum Token {
    tok_eof = -1,

    //command
    tok_def = -2,
    tok_extern = -3,

    //primary
    tok_identifier = -4,
    tok_number = -5
};

static std::string IdentifierStr; //filled in if tok_identifier
static double NumVal;             //filled in if tok_number

//gettok - return the next token form standard input
static int gettok() {
    static int LastChar = ' ';
    
    //skip whitespace
    while(isspace(LastChar)) {
        LastChar = getchar();
    }
    if(isalpha(LastChar)) {
        IdentifierStr = LastChar;
        while(isalnum(LastChar = getchar())) {
            IdentifierStr += LastChar;
        }

        if(IdentifierStr == "def") {
            return tok_def;
        }
        if(IdentifierStr == "extern") {
            return tok_extern;
        }
        return tok_identifier;
    }

    if(isdigit(LastChar) || LastChar == '.') {
        std::string NumStr;
        do {
            NumStr += LastChar;
        } while (isdigit(LastChar) || LastChar == '.');

        NumVal = strtod(NumStr.c_str(), nullptr);
        return tok_number;
    }

    if(LastChar == '#') {
        // comment until end of line
        do
        {
            /* code */
            LastChar = getchar();
        } while (LastChar != EOF && LastChar != '\n' && LastChar != '\r');

        if (LastChar != EOF) {
            return gettok();
        }
    }

    //check for end of file,dont eat eof
    if (LastChar == EOF) {
        return tok_eof;
    }

    // other wise, just return the character as its ascii value
    int ThisChar = LastChar;
    LastChar = getchar();
    return ThisChar;

}

///=== abstract syntax tree

namespace {
    /// ExprAST - Base class for all expression nodes.
    class ExprAST {
    public:
        virtual ~ExprAST() = default;
    };

    // NumberExprAST - expression class for numeric literals like "1.0"
    class NumberExprAST : public ExprAST {
        double Val;

    public:
        NumberExprAST(double Val) : Val(Val) {}
    };

    // VariableExprAST - expression class for referencing a variable, like "a".
    class VariableExprAST : public ExprAST {
        std::string Name;
    public:
        VariableExprAST(const std::string &Name) : Name(Name) {}
    };

    //BinaryExprAST - expression class for a binary operator.
    class BinaryExprAST : public ExprAST {
        char Op;
        std::unique_ptr<ExprAST> LHS, RHS;
    public:
        BinaryExprAST(char Op, std::unique_ptr<ExprAST> LHS, std::unique_ptr<ExprAST>) 
        : Op(Op), LHS(std::move(LHS)), RHS(std::move(RHS)) {}
    };

    // CallExprAST - expression class for function calls
    class CallExprAST : public ExprAST {
        std::string Callee;
        std::vector<std::unique_ptr<ExprAST>> Args;
    public:
        CallExprAST(const std::string &Callee, std::vector<std::unique_ptr<ExprAST>> Args)
        : Callee(Callee), Args(std::move(Args)) {}
    };

    /// PrototypeAST - This class represents the "prototype" for a function,
    /// which captures its name, and its argument names (thus implicitly the number
    /// of arguments the function takes).
    class PrototypeAST {
        std::string Name;
        std::vector<std::string> Args;
    public:
        PrototypeAST(const std::string &Name, std::vector<std::string> Args)
        : Name(Name), Args(std::move(Args)) {}

        const std::string &getName() const { return Name; }
    };

    /// FunctionAST - This class represents a function definition itself.
    class FunctionAST {
        std::unique_ptr<PrototypeAST> Proto;
        std::unique_ptr<ExprAST> Body;
    public:
        FunctionAST(std::unique_ptr<PrototypeAST> Proto,
                    std::unique_ptr<ExprAST> Body)
                : Proto(std::move(Proto)), Body(std::move(Body)) {}
    };
} // namespace name


///==== Parser ====

// CurTok/getNextToken - provide a simple token buffer.
// CurTok is the current token the parser is looking at.
// getNextToken reads another token fromt the lexer and 
// updates CurTok with its results.

static int CurTok;
static int getNextToken() { return CurTok = gettok(); }

//Binoprecedence - this holds the precedence for each binary operator defined
static std::map<char, int> BinoPrecedence;

/// GetTokPrecedence - Get the precedence of the pending binary operator token.
static int GetTokPrecedence() {
    if (!isascii(CurTok)) {
        return -1;
    }
    int TokPrec = BinoPrecedence[CurTok];
    if (TokPrec <= 0) {
        return -1;
    }
    return TokPrec;
}

/// LogError* - These are little helper functions for error handling.
std::unique_ptr<ExprAST> LogError(const char *Str) {
    fprintf(stderr, "Error: %s\n", Str);
    return nullptr;
}
std::unique_ptr<PrototypeAST> LogErrorP(const char *Str) {
    LogError(Str);
    return nullptr;
}

static std::unique_ptr<ExprAST> ParseExpression();

/// numberexpr ::= number
static std::unique_ptr<ExprAST> ParseNumberExpr() {
    auto Result = std::make_unique<NumberExprAST>(NumVal);
    getNextToken();
    return std::move(Result);
}

/// parenexpr ::= '(' expressions ')'
static std::unique_ptr<ExprAST> ParseParentExpr() {
    getNextToken(); //eat (
    auto V = ParseExpression();
    if(!V) {
        return nullptr;
    }

    if(CurTok != ')') {
        return LogError("expected ')");
    }
    getNextToken(); // eat )
    return V;
}

/// identifierexpr
///   ::= identifier
///   ::= identifier '(' expression* ')'
static std::unique_ptr<ExprAST> ParseIdentifierExpr () {
    std::string IdName = IdentifierStr;

    getNextToken(); // eat IdentifierStr

    if(CurTok != '(') {  // Simple variable ref.
        return std::make_unique<VariableExprAST>(IdName);
    }
    //call
    getNextToken(); // eat(
    std::vector<std::unique_ptr<ExprAST>> Args;
    if(CurTok != ')') {
        while(true) {
            if(auto Arg = ParseExpression()) {
                Args.push_back(std::move(Arg));
            } else {
                return nullptr;
            }
            
            if(CurTok == ')') {
                break;
            }

            if(CurTok != ',') {
                return LogError("Expected ') or ',' int arguments list" );
            }
            getNextToken;
        }
    }
    //eat ')'
    getNextToken();
    
    return std::make_unique<CallExprAST>(IdName, std::move(Args));
}

/// primary
///   ::= identifierexpr
///   ::= numberexpr
///   ::= parenexpr

static std::unique_ptr<ExprAST> ParsePrimary() {
    switch (CurTok) {
        default:
            return LogError("unknown token when expecting an expression");
        case tok_identifier:
            return ParseIdentifierExpr();
        case tok_number:
            return ParseNumberExpr();
        case '(':
            return ParseParentExpr();
    }
}

/// binoprhs
///   ::= ('+' primary)*
static std::unique_ptr<ExprAST> ParseBinOpRHS(int ExprPrec, std::unique_ptr<ExprAST> LHS) {
    // if this is a binop, find its precedence
    while(true) {
        int TokPrec = GetTokPrecedence();
        // If this is a binop that binds at least as tightly as the current binop,
        // consume it, otherwise we are done.
        if(TokPrec < ExprPrec) {
            return LHS;
        }

        int BinOp = CurTok;
        getNextToken(); // eat binop

        // parse the primary expression after teh binary operator
        auto RHS = ParsePrimary();
        if(!RHS) {
            return nullptr;
        }

        // If BinOp binds less tightly with RHS than the operator after RHS, let
        // the pending operator take RHS as its LHS.
        int NextPrec = GetTokPrecedence();
        if (TokPrec < NextPrec) {
            RHS = ParseBinOpRHS(TokPrec + 1, std::move(RHS));
            if(!RHS) {
                return nullptr;
            }
        }

        //merge LHS/RHS
        LHS = std::make_unique<BinaryExprAST>(BinOp, std::move(LHS), std::move(RHS));
    }
}

/// expression
///   ::= primary binoprhs
///
static std::unique_ptr<ExprAST> ParseExpression() {
    auto LHS = ParsePrimary();
    if(!LHS) {
        return nullptr;
    }
    return ParseBinOpRHS(0, std::move(LHS));
}

/// prototype
///   ::= id '(' id* ')'
static std::unique_ptr<PrototypeAST> ParsePrototype() {
    if(CurTok != tok_identifier) {
        return LogErrorP("Expected function name in prototype");
    }

    std::string FnName = IdentifierStr;
    getNextToken();

    if(CurTok != '(') {
        return LogErrorP("Expected '(' in prototype");
    }

    std::vector<std::string> ArgNames;
    while (getNextToken() == tok_identifier) {
        ArgNames.push_back(IdentifierStr);
    }
    if(CurTok != ')') {
        return LogErrorP("Expected ')' in prototype");
    }

    //success
    getNextToken(); // eat ')'

    return std::make_unique<PrototypeAST>(FnName, std::move(ArgNames));
}

/// definition ::= 'def' prototype expression
static std::unique_ptr<FunctionAST> ParseDefinition() {
    getNextToken(); // eat token
    auto Proto = ParsePrototype();
    if(!Proto) {
        return nullptr;
    }

    if( auto E = ParseExpression() ) {
        return std::make_unique<FunctionAST>(std::move(Proto), std::move(E));
    }
    return nullptr;
}

/// toplevelexpr ::= expression
static std::unique_ptr<FunctionAST> ParseTopLevelExpr() {
    if( auto E = ParseExpression() ) {
        // make an anonymous proto.
        auto Proto = std::make_unique<PrototypeAST>("__anon_expr",
                                                    std::vector<std::string>());
        return std::make_unique<FunctionAST>(std::move(Proto), std::move(E));
    }
    return nullptr;
}

/// external ::= 'extern' prototype
static std::unique_ptr<PrototypeAST> ParseExtern() {
    getNextToken(); // eat extern
    return ParsePrototype();
}

/// ==== top level parsing ====

static void HandleDefinition() {
    if (ParseDefinition()) {
        fprintf(stderr, "Parsed a function definition.\n");
    } else {
        // skip token for error recovery.
        getNextToken;
    }
}

static void HandleExtern() {
    if (ParseExtern()) {
        fprintf(stderr, "Parsed a function definition.\n");
    } else {
        // skip token for error recovery
        getNextToken();
    }
}

static void HandleTopLevelExpression() {
    // Evaluate a top-level expression into an anonymous function.
    if(ParseTopLevelExpr()) {
        fprintf(stderr, "Parsed a top-level expr\n");
    } else {
        // skip token for error recovery
        getNextToken();
    }
}

/// top ::= definition | external | expression | ';'

static void MainLoop () {
    while (true) {
        fprintf(stderr, "ready> ");
        switch (CurTok) {
        case tok_eof:
           return;
        case ';':
            getNextToken();
            break;
        case tok_def:
            HandleDefinition();
            break;
        case tok_extern:
            HandleExtern();
            break;
        default:
            HandleTopLevelExpression();
            break;
        }
    }
}
int main() {
    BinoPrecedence['<'] = 10;
    BinoPrecedence['+'] = 20;
    BinoPrecedence['-'] = 20;
    BinoPrecedence['*'] = 40;

    //prime for firset token
    fprintf(stderr, "ready> ");
    getNextToken();

    //run the main "interpreter loop"
    MainLoop();

    return 0;
}                                                                                                                     