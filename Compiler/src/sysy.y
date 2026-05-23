%language "c++"
%skeleton "lalr1.cc"
%defines
%define api.namespace {sysy}
%define api.parser.class {Parser}
%define api.token.constructor
%define api.value.type variant
%define parse.error verbose

%code requires {
#include "ast.hpp"

#include <memory>
#include <string>
#include <vector>

namespace sysy {

// Driver 是 lexer、parser 和 main 之间共享的轻量上下文。
// parser 只负责把源码规约成 AST 根节点，不在语义动作里生成 Koopa IR。
struct Driver {
  std::unique_ptr<CompUnitAST> ast_root;
  std::string filename;
  int line = 1;
  int column = 1;
};

}  // namespace sysy
}

%code {
#include <sstream>
#include <utility>
#include <vector>

namespace sysy {
Parser::symbol_type yylex(Driver& driver);
}  // namespace sysy
}

%parse-param { sysy::Driver& driver }
%lex-param { sysy::Driver& driver }

%token INT "int"
%token FLOAT "float"
%token VOID "void"
%token CONST "const"
%token RETURN "return"
%token IF "if"
%token ELSE "else"
%token WHILE "while"
%token BREAK "break"
%token CONTINUE "continue"
%token L_PAREN "("
%token R_PAREN ")"
%token L_BRACKET "["
%token R_BRACKET "]"
%token L_BRACE "{"
%token R_BRACE "}"
%token SEMICOLON ";"
%token COMMA ","
%token ASSIGN "="
%token PLUS "+"
%token MINUS "-"
%token STAR "*"
%token SLASH "/"
%token PERCENT "%"
%token NOT "!"
%token LT "<"
%token GT ">"
%token LE "<="
%token GE ">="
%token EQ "=="
%token NE "!="
%token LAND "&&"
%token LOR "||"
%token <std::string> IDENT
%token <int> INT_CONST
%token <double> FLOAT_CONST

%type <sysy::BaseAST*> CompUnitItem BlockItem Decl ConstDecl VarDecl
%type <sysy::CompUnitAST*> CompUnitItemList
%type <sysy::BlockAST*> Block BlockItemList
%type <sysy::StmtAST*> Stmt
%type <sysy::LValAST*> LVal
%type <sysy::VarDefAST*> ConstDef VarDef
%type <std::vector<std::unique_ptr<sysy::VarDefAST>>*> ConstDefList VarDefList
%type <sysy::FuncFParamAST*> FuncFParam
%type <std::vector<std::unique_ptr<sysy::FuncFParamAST>>*> FuncFParams
%type <std::vector<std::unique_ptr<sysy::ExprAST>>*> FuncRParams
%type <std::vector<std::unique_ptr<sysy::VarDefAST>>*> TopVarDefListTail
%type <std::vector<int>*> ArrayDims ArrayDimsTail
%type <std::vector<std::unique_ptr<sysy::ExprAST>>*> IndexList
%type <sysy::InitValAST*> InitVal ConstInitVal TopVarInit
%type <std::vector<std::unique_ptr<sysy::InitValAST>>*> InitValList ConstInitValList
%type <sysy::ExprAST*> Exp LOrExp LAndExp EqExp RelExp AddExp MulExp UnaryExp
%type <sysy::ExprAST*> PrimaryExp Number ConstExp
%type <sysy::Type> BType

%nonassoc LOWER_THAN_ELSE
%nonassoc ELSE

%start CompUnit

%%

CompUnit
  : CompUnitItemList
    {
      driver.ast_root.reset($1);
    }
  ;

CompUnitItemList
  : CompUnitItem
    {
      $$ = new sysy::CompUnitAST();
      $$->AddUnit(std::unique_ptr<sysy::BaseAST>($1));
    }
  | CompUnitItemList CompUnitItem
    {
      $$ = $1;
      $$->AddUnit(std::unique_ptr<sysy::BaseAST>($2));
    }
  ;

CompUnitItem
  : CONST BType ConstDefList SEMICOLON
    {
      $$ = new sysy::ConstDeclAST($2, std::move(*$3));
      delete $3;
    }
  | INT IDENT L_PAREN R_PAREN Block
    {
      std::vector<std::unique_ptr<sysy::FuncFParamAST>> params;
      $$ = new sysy::FuncDefAST(sysy::Type::Int(), std::move($2),
                                std::move(params),
                                std::unique_ptr<sysy::BlockAST>($5));
    }
  | INT IDENT L_PAREN FuncFParams R_PAREN Block
    {
      $$ = new sysy::FuncDefAST(sysy::Type::Int(), std::move($2),
                                std::move(*$4),
                                std::unique_ptr<sysy::BlockAST>($6));
      delete $4;
    }
  | FLOAT IDENT L_PAREN R_PAREN Block
    {
      std::vector<std::unique_ptr<sysy::FuncFParamAST>> params;
      $$ = new sysy::FuncDefAST(sysy::Type::Float(), std::move($2),
                                std::move(params),
                                std::unique_ptr<sysy::BlockAST>($5));
    }
  | FLOAT IDENT L_PAREN FuncFParams R_PAREN Block
    {
      $$ = new sysy::FuncDefAST(sysy::Type::Float(), std::move($2),
                                std::move(*$4),
                                std::unique_ptr<sysy::BlockAST>($6));
      delete $4;
    }
  | INT IDENT ArrayDims TopVarInit TopVarDefListTail SEMICOLON
    {
      std::vector<std::unique_ptr<sysy::VarDefAST>> defs;
      defs.push_back(sysy::VarDefAST::MakeArray(
          std::move($2), std::move(*$3),
          std::unique_ptr<sysy::InitValAST>($4)));
      delete $3;
      for (auto& def : *$5) {
        defs.push_back(std::move(def));
      }
      delete $5;
      $$ = new sysy::VarDeclAST(sysy::Type::Int(), std::move(defs));
    }
  | FLOAT IDENT ArrayDims TopVarInit TopVarDefListTail SEMICOLON
    {
      std::vector<std::unique_ptr<sysy::VarDefAST>> defs;
      defs.push_back(sysy::VarDefAST::MakeArray(
          std::move($2), std::move(*$3),
          std::unique_ptr<sysy::InitValAST>($4)));
      delete $3;
      for (auto& def : *$5) {
        defs.push_back(std::move(def));
      }
      delete $5;
      $$ = new sysy::VarDeclAST(sysy::Type::Float(), std::move(defs));
    }
  | FLOAT IDENT TopVarInit TopVarDefListTail SEMICOLON
    {
      std::vector<std::unique_ptr<sysy::VarDefAST>> defs;
      if ($3 == nullptr) {
        defs.push_back(sysy::VarDefAST::MakeInt(std::move($2)));
      } else {
        defs.push_back(std::unique_ptr<sysy::VarDefAST>(
            new sysy::VarDefAST(sysy::Type::Float(), std::move($2),
                                std::unique_ptr<sysy::InitValAST>($3))));
      }
      for (auto& def : *$4) {
        defs.push_back(std::move(def));
      }
      delete $4;
      $$ = new sysy::VarDeclAST(sysy::Type::Float(), std::move(defs));
    }
  | INT IDENT TopVarInit TopVarDefListTail SEMICOLON
    {
      std::vector<std::unique_ptr<sysy::VarDefAST>> defs;
      if ($3 == nullptr) {
        defs.push_back(sysy::VarDefAST::MakeInt(std::move($2)));
      } else {
        defs.push_back(std::unique_ptr<sysy::VarDefAST>(
            new sysy::VarDefAST(sysy::Type::Int(), std::move($2),
                                std::unique_ptr<sysy::InitValAST>($3))));
      }
      for (auto& def : *$4) {
        defs.push_back(std::move(def));
      }
      delete $4;
      $$ = new sysy::VarDeclAST(sysy::Type::Int(), std::move(defs));
    }
  | VOID IDENT L_PAREN R_PAREN Block
    {
      std::vector<std::unique_ptr<sysy::FuncFParamAST>> params;
      $$ = new sysy::FuncDefAST(sysy::Type::Void(), std::move($2),
                                std::move(params),
                                std::unique_ptr<sysy::BlockAST>($5));
    }
  | VOID IDENT L_PAREN FuncFParams R_PAREN Block
    {
      $$ = new sysy::FuncDefAST(sysy::Type::Void(), std::move($2),
                                std::move(*$4),
                                std::unique_ptr<sysy::BlockAST>($6));
      delete $4;
    }
  ;

TopVarInit
  : %empty
    {
      $$ = nullptr;
    }
  | ASSIGN InitVal
    {
      $$ = $2;
    }
  ;

TopVarDefListTail
  : %empty
    {
      $$ = new std::vector<std::unique_ptr<sysy::VarDefAST>>();
    }
  | COMMA VarDefList
    {
      $$ = $2;
    }
  ;

FuncFParams
  : FuncFParam
    {
      $$ = new std::vector<std::unique_ptr<sysy::FuncFParamAST>>();
      $$->push_back(std::unique_ptr<sysy::FuncFParamAST>($1));
    }
  | FuncFParams COMMA FuncFParam
    {
      $$ = $1;
      $$->push_back(std::unique_ptr<sysy::FuncFParamAST>($3));
    }
  ;

FuncFParam
  : BType IDENT
    {
      $$ = new sysy::FuncFParamAST($1, std::move($2));
    }
  | BType IDENT L_BRACKET R_BRACKET ArrayDimsTail
    {
      $$ = new sysy::FuncFParamAST(
          $1.basic == sysy::BasicType::kFloat
              ? sysy::Type::FloatArrayParameter(std::move(*$5))
              : sysy::Type::IntArrayParameter(std::move(*$5)),
          std::move($2));
      delete $5;
    }
  ;

Block
  : L_BRACE BlockItemList R_BRACE
    {
      $$ = $2;
    }
  ;

BlockItemList
  : %empty
    {
      $$ = new sysy::BlockAST();
    }
  | BlockItemList BlockItem
    {
      $$ = $1;
      $$->AddItem(std::unique_ptr<sysy::BaseAST>($2));
    }
  ;

BlockItem
  : Decl
    {
      $$ = $1;
    }
  | Stmt
    {
      $$ = $1;
    }
  ;

Decl
  : ConstDecl
    {
      $$ = $1;
    }
  | VarDecl
    {
      $$ = $1;
    }
  ;

ConstDecl
  : CONST BType ConstDefList SEMICOLON
    {
      $$ = new sysy::ConstDeclAST($2, std::move(*$3));
      delete $3;
    }
  ;

ConstDefList
  : ConstDef
    {
      $$ = new std::vector<std::unique_ptr<sysy::VarDefAST>>();
      $$->push_back(std::unique_ptr<sysy::VarDefAST>($1));
    }
  | ConstDefList COMMA ConstDef
    {
      $$ = $1;
      $$->push_back(std::unique_ptr<sysy::VarDefAST>($3));
    }
  ;

ConstDef
  : IDENT ASSIGN ConstInitVal
    {
      $$ = new sysy::VarDefAST(sysy::Type::Int(), std::move($1),
                               std::unique_ptr<sysy::InitValAST>($3));
    }
  | IDENT ArrayDims ASSIGN ConstInitVal
    {
      $$ = sysy::VarDefAST::MakeArray(
          std::move($1), std::move(*$2),
          std::unique_ptr<sysy::InitValAST>($4)).release();
      delete $2;
    }
  ;

ConstInitVal
  : ConstExp
    {
      $$ = sysy::InitValAST::MakeScalar(
          std::unique_ptr<sysy::ExprAST>($1)).release();
    }
  | L_BRACE R_BRACE
    {
      std::vector<std::unique_ptr<sysy::InitValAST>> elements;
      $$ = sysy::InitValAST::MakeAggregate(std::move(elements)).release();
    }
  | L_BRACE ConstInitValList R_BRACE
    {
      $$ = sysy::InitValAST::MakeAggregate(std::move(*$2)).release();
      delete $2;
    }
  ;

ConstInitValList
  : ConstInitVal
    {
      $$ = new std::vector<std::unique_ptr<sysy::InitValAST>>();
      $$->push_back(std::unique_ptr<sysy::InitValAST>($1));
    }
  | ConstInitValList COMMA ConstInitVal
    {
      $$ = $1;
      $$->push_back(std::unique_ptr<sysy::InitValAST>($3));
    }
  ;

VarDecl
  : BType VarDefList SEMICOLON
    {
      $$ = new sysy::VarDeclAST($1, std::move(*$2));
      delete $2;
    }
  ;

VarDefList
  : VarDef
    {
      $$ = new std::vector<std::unique_ptr<sysy::VarDefAST>>();
      $$->push_back(std::unique_ptr<sysy::VarDefAST>($1));
    }
  | VarDefList COMMA VarDef
    {
      $$ = $1;
      $$->push_back(std::unique_ptr<sysy::VarDefAST>($3));
    }
  ;

VarDef
  : IDENT
    {
      $$ = sysy::VarDefAST::MakeInt(std::move($1)).release();
    }
  | IDENT ASSIGN InitVal
    {
      $$ = new sysy::VarDefAST(sysy::Type::Int(), std::move($1),
                               std::unique_ptr<sysy::InitValAST>($3));
    }
  | IDENT ArrayDims
    {
      $$ = sysy::VarDefAST::MakeArray(std::move($1), std::move(*$2),
                                      nullptr).release();
      delete $2;
    }
  | IDENT ArrayDims ASSIGN InitVal
    {
      $$ = sysy::VarDefAST::MakeArray(
          std::move($1), std::move(*$2),
          std::unique_ptr<sysy::InitValAST>($4)).release();
      delete $2;
    }
  ;

InitVal
  : Exp
    {
      $$ = sysy::InitValAST::MakeScalar(
          std::unique_ptr<sysy::ExprAST>($1)).release();
    }
  | L_BRACE R_BRACE
    {
      std::vector<std::unique_ptr<sysy::InitValAST>> elements;
      $$ = sysy::InitValAST::MakeAggregate(std::move(elements)).release();
    }
  | L_BRACE InitValList R_BRACE
    {
      $$ = sysy::InitValAST::MakeAggregate(std::move(*$2)).release();
      delete $2;
    }
  ;

InitValList
  : InitVal
    {
      $$ = new std::vector<std::unique_ptr<sysy::InitValAST>>();
      $$->push_back(std::unique_ptr<sysy::InitValAST>($1));
    }
  | InitValList COMMA InitVal
    {
      $$ = $1;
      $$->push_back(std::unique_ptr<sysy::InitValAST>($3));
    }
  ;

BType
  : INT
    {
      $$ = sysy::Type::Int();
    }
  | FLOAT
    {
      $$ = sysy::Type::Float();
    }
  ;

Stmt
  : LVal ASSIGN Exp SEMICOLON
    {
      $$ = sysy::StmtAST::MakeAssign(
          std::unique_ptr<sysy::LValAST>($1),
          std::unique_ptr<sysy::ExprAST>($3)).release();
    }
  | RETURN Exp SEMICOLON
    {
      $$ = sysy::StmtAST::MakeReturn(
          std::unique_ptr<sysy::ExprAST>($2)).release();
    }
  | RETURN SEMICOLON
    {
      $$ = sysy::StmtAST::MakeReturnVoid().release();
    }
  | Exp SEMICOLON
    {
      $$ = sysy::StmtAST::MakeExpr(
          std::unique_ptr<sysy::ExprAST>($1)).release();
    }
  | SEMICOLON
    {
      $$ = sysy::StmtAST::MakeEmpty().release();
    }
  | Block
    {
      $$ = sysy::StmtAST::MakeBlock(
          std::unique_ptr<sysy::BlockAST>($1)).release();
    }
  | IF L_PAREN Exp R_PAREN Stmt %prec LOWER_THAN_ELSE
    {
      $$ = sysy::StmtAST::MakeIf(
          std::unique_ptr<sysy::ExprAST>($3),
          std::unique_ptr<sysy::StmtAST>($5), nullptr).release();
    }
  | IF L_PAREN Exp R_PAREN Stmt ELSE Stmt
    {
      $$ = sysy::StmtAST::MakeIf(
          std::unique_ptr<sysy::ExprAST>($3),
          std::unique_ptr<sysy::StmtAST>($5),
          std::unique_ptr<sysy::StmtAST>($7)).release();
    }
  | WHILE L_PAREN Exp R_PAREN Stmt
    {
      $$ = sysy::StmtAST::MakeWhile(
          std::unique_ptr<sysy::ExprAST>($3),
          std::unique_ptr<sysy::StmtAST>($5)).release();
    }
  | BREAK SEMICOLON
    {
      $$ = sysy::StmtAST::MakeBreak().release();
    }
  | CONTINUE SEMICOLON
    {
      $$ = sysy::StmtAST::MakeContinue().release();
    }
  ;

Exp
  : LOrExp
    {
      $$ = $1;
    }
  ;

LOrExp
  : LAndExp
    {
      $$ = $1;
    }
  | LOrExp LOR LAndExp
    {
      $$ = sysy::MakeBinary(
          sysy::BinaryOp::kOr, std::unique_ptr<sysy::ExprAST>($1),
          std::unique_ptr<sysy::ExprAST>($3)).release();
    }
  ;

LAndExp
  : EqExp
    {
      $$ = $1;
    }
  | LAndExp LAND EqExp
    {
      $$ = sysy::MakeBinary(
          sysy::BinaryOp::kAnd, std::unique_ptr<sysy::ExprAST>($1),
          std::unique_ptr<sysy::ExprAST>($3)).release();
    }
  ;

EqExp
  : RelExp
    {
      $$ = $1;
    }
  | EqExp EQ RelExp
    {
      $$ = sysy::MakeBinary(
          sysy::BinaryOp::kEq, std::unique_ptr<sysy::ExprAST>($1),
          std::unique_ptr<sysy::ExprAST>($3)).release();
    }
  | EqExp NE RelExp
    {
      $$ = sysy::MakeBinary(
          sysy::BinaryOp::kNe, std::unique_ptr<sysy::ExprAST>($1),
          std::unique_ptr<sysy::ExprAST>($3)).release();
    }
  ;

RelExp
  : AddExp
    {
      $$ = $1;
    }
  | RelExp LT AddExp
    {
      $$ = sysy::MakeBinary(
          sysy::BinaryOp::kLt, std::unique_ptr<sysy::ExprAST>($1),
          std::unique_ptr<sysy::ExprAST>($3)).release();
    }
  | RelExp GT AddExp
    {
      $$ = sysy::MakeBinary(
          sysy::BinaryOp::kGt, std::unique_ptr<sysy::ExprAST>($1),
          std::unique_ptr<sysy::ExprAST>($3)).release();
    }
  | RelExp LE AddExp
    {
      $$ = sysy::MakeBinary(
          sysy::BinaryOp::kLe, std::unique_ptr<sysy::ExprAST>($1),
          std::unique_ptr<sysy::ExprAST>($3)).release();
    }
  | RelExp GE AddExp
    {
      $$ = sysy::MakeBinary(
          sysy::BinaryOp::kGe, std::unique_ptr<sysy::ExprAST>($1),
          std::unique_ptr<sysy::ExprAST>($3)).release();
    }
  ;

AddExp
  : MulExp
    {
      $$ = $1;
    }
  | AddExp PLUS MulExp
    {
      $$ = sysy::MakeBinary(
          sysy::BinaryOp::kAdd, std::unique_ptr<sysy::ExprAST>($1),
          std::unique_ptr<sysy::ExprAST>($3)).release();
    }
  | AddExp MINUS MulExp
    {
      $$ = sysy::MakeBinary(
          sysy::BinaryOp::kSub, std::unique_ptr<sysy::ExprAST>($1),
          std::unique_ptr<sysy::ExprAST>($3)).release();
    }
  ;

MulExp
  : UnaryExp
    {
      $$ = $1;
    }
  | MulExp STAR UnaryExp
    {
      $$ = sysy::MakeBinary(
          sysy::BinaryOp::kMul, std::unique_ptr<sysy::ExprAST>($1),
          std::unique_ptr<sysy::ExprAST>($3)).release();
    }
  | MulExp SLASH UnaryExp
    {
      $$ = sysy::MakeBinary(
          sysy::BinaryOp::kDiv, std::unique_ptr<sysy::ExprAST>($1),
          std::unique_ptr<sysy::ExprAST>($3)).release();
    }
  | MulExp PERCENT UnaryExp
    {
      $$ = sysy::MakeBinary(
          sysy::BinaryOp::kMod, std::unique_ptr<sysy::ExprAST>($1),
          std::unique_ptr<sysy::ExprAST>($3)).release();
    }
  ;

UnaryExp
  : PrimaryExp
    {
      $$ = $1;
    }
  | IDENT L_PAREN R_PAREN
    {
      std::vector<std::unique_ptr<sysy::ExprAST>> args;
      $$ = sysy::MakeCall(std::move($1), std::move(args)).release();
    }
  | IDENT L_PAREN FuncRParams R_PAREN
    {
      $$ = sysy::MakeCall(std::move($1), std::move(*$3)).release();
      delete $3;
    }
  | PLUS UnaryExp
    {
      $$ = sysy::MakeUnary(
          sysy::UnaryOp::kPlus,
          std::unique_ptr<sysy::ExprAST>($2)).release();
    }
  | MINUS UnaryExp
    {
      $$ = sysy::MakeUnary(
          sysy::UnaryOp::kMinus,
          std::unique_ptr<sysy::ExprAST>($2)).release();
    }
  | NOT UnaryExp
    {
      $$ = sysy::MakeUnary(
          sysy::UnaryOp::kNot,
          std::unique_ptr<sysy::ExprAST>($2)).release();
    }
  ;

FuncRParams
  : Exp
    {
      $$ = new std::vector<std::unique_ptr<sysy::ExprAST>>();
      $$->push_back(std::unique_ptr<sysy::ExprAST>($1));
    }
  | FuncRParams COMMA Exp
    {
      $$ = $1;
      $$->push_back(std::unique_ptr<sysy::ExprAST>($3));
    }
  ;

PrimaryExp
  : Number
    {
      $$ = $1;
    }
  | LVal
    {
      $$ = $1;
    }
  | L_PAREN Exp R_PAREN
    {
      $$ = $2;
    }
  ;

Number
  : INT_CONST
    {
      $$ = sysy::MakeNumber($1).release();
    }
  | FLOAT_CONST
    {
      $$ = sysy::MakeFloatNumber($1).release();
    }
  ;

LVal
  : IDENT
    {
      $$ = sysy::MakeLVal(std::move($1)).release();
    }
  | IDENT IndexList
    {
      $$ = sysy::MakeLVal(std::move($1), std::move(*$2)).release();
      delete $2;
    }
  ;

IndexList
  : L_BRACKET Exp R_BRACKET
    {
      $$ = new std::vector<std::unique_ptr<sysy::ExprAST>>();
      $$->push_back(std::unique_ptr<sysy::ExprAST>($2));
    }
  | IndexList L_BRACKET Exp R_BRACKET
    {
      $$ = $1;
      $$->push_back(std::unique_ptr<sysy::ExprAST>($3));
    }
  ;

ConstExp
  : Exp
    {
      $$ = $1;
    }
  ;

ArrayDims
  : L_BRACKET INT_CONST R_BRACKET
    {
      if ($2 <= 0) {
        throw sysy::SemanticError("数组维度必须大于 0");
      }
      $$ = new std::vector<int>();
      $$->push_back($2);
    }
  | ArrayDims L_BRACKET INT_CONST R_BRACKET
    {
      if ($3 <= 0) {
        throw sysy::SemanticError("数组维度必须大于 0");
      }
      $$ = $1;
      $$->push_back($3);
    }
  ;

ArrayDimsTail
  : %empty
    {
      $$ = new std::vector<int>();
    }
  | ArrayDimsTail L_BRACKET INT_CONST R_BRACKET
    {
      if ($3 <= 0) {
        throw sysy::SemanticError("数组维度必须大于 0");
      }
      $$ = $1;
      $$->push_back($3);
    }
  ;

%%

void sysy::Parser::error(const std::string& message) {
  std::ostringstream oss;
  oss << message;
  throw sysy::SemanticError(oss.str());
}
