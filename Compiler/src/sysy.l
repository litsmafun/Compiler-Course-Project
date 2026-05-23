%option noyywrap nounput noinput batch nounistd
%x COMMENT

%{
#include "sysy_parser.hpp"

#include <cstdlib>
#include <string>

#ifdef _MSC_VER
#include <io.h>
#define isatty _isatty
#define fileno _fileno
#endif

namespace sysy {
Parser::symbol_type yylex(Driver& driver);
}  // namespace sysy

#define YY_DECL sysy::Parser::symbol_type sysy::yylex(sysy::Driver& driver)

namespace {

void AdvanceColumns(sysy::Driver& driver, int length) {
  driver.column += length;
}

void AdvanceLine(sysy::Driver& driver) {
  ++driver.line;
  driver.column = 1;
}

int ParseInteger(const char* text) {
  return static_cast<int>(std::strtol(text, nullptr, 0));
}

double ParseFloat(const char* text) {
  return std::stod(text);
}

}  // namespace
%}

DIGIT      [0-9]
HEX_DIGIT  [0-9a-fA-F]
EXP        [eE][+-]?{DIGIT}+
FLOAT_LIT  (({DIGIT}+\.{DIGIT}*)|(\.{DIGIT}+))({EXP})?|{DIGIT}+{EXP}
IDENT_HEAD [A-Za-z_]
IDENT_TAIL [A-Za-z0-9_]

%%

[ \t\r]+ {
  AdvanceColumns(driver, yyleng);
}

\n {
  AdvanceLine(driver);
}

"//"[^\n]* {
  AdvanceColumns(driver, yyleng);
}

"/*" {
  AdvanceColumns(driver, yyleng);
  BEGIN(COMMENT);
}

<COMMENT>"*/" {
  AdvanceColumns(driver, yyleng);
  BEGIN(INITIAL);
}

<COMMENT>\n {
  AdvanceLine(driver);
}

<COMMENT>. {
  AdvanceColumns(driver, yyleng);
}

<COMMENT><<EOF>> {
  throw sysy::SemanticError("unterminated block comment");
}

"int" {
  AdvanceColumns(driver, yyleng);
  return sysy::Parser::make_INT();
}

"float" {
  AdvanceColumns(driver, yyleng);
  return sysy::Parser::make_FLOAT();
}

"void" {
  AdvanceColumns(driver, yyleng);
  return sysy::Parser::make_VOID();
}

"const" {
  AdvanceColumns(driver, yyleng);
  return sysy::Parser::make_CONST();
}

"return" {
  AdvanceColumns(driver, yyleng);
  return sysy::Parser::make_RETURN();
}

"if" {
  AdvanceColumns(driver, yyleng);
  return sysy::Parser::make_IF();
}

"else" {
  AdvanceColumns(driver, yyleng);
  return sysy::Parser::make_ELSE();
}

"while" {
  AdvanceColumns(driver, yyleng);
  return sysy::Parser::make_WHILE();
}

"break" {
  AdvanceColumns(driver, yyleng);
  return sysy::Parser::make_BREAK();
}

"continue" {
  AdvanceColumns(driver, yyleng);
  return sysy::Parser::make_CONTINUE();
}

"(" {
  AdvanceColumns(driver, yyleng);
  return sysy::Parser::make_L_PAREN();
}

")" {
  AdvanceColumns(driver, yyleng);
  return sysy::Parser::make_R_PAREN();
}

"[" {
  AdvanceColumns(driver, yyleng);
  return sysy::Parser::make_L_BRACKET();
}

"]" {
  AdvanceColumns(driver, yyleng);
  return sysy::Parser::make_R_BRACKET();
}

"{" {
  AdvanceColumns(driver, yyleng);
  return sysy::Parser::make_L_BRACE();
}

"}" {
  AdvanceColumns(driver, yyleng);
  return sysy::Parser::make_R_BRACE();
}

";" {
  AdvanceColumns(driver, yyleng);
  return sysy::Parser::make_SEMICOLON();
}

"," {
  AdvanceColumns(driver, yyleng);
  return sysy::Parser::make_COMMA();
}

"=" {
  AdvanceColumns(driver, yyleng);
  return sysy::Parser::make_ASSIGN();
}

"==" {
  AdvanceColumns(driver, yyleng);
  return sysy::Parser::make_EQ();
}

"!=" {
  AdvanceColumns(driver, yyleng);
  return sysy::Parser::make_NE();
}

"&&" {
  AdvanceColumns(driver, yyleng);
  return sysy::Parser::make_LAND();
}

"||" {
  AdvanceColumns(driver, yyleng);
  return sysy::Parser::make_LOR();
}

"<=" {
  AdvanceColumns(driver, yyleng);
  return sysy::Parser::make_LE();
}

">=" {
  AdvanceColumns(driver, yyleng);
  return sysy::Parser::make_GE();
}

"+" {
  AdvanceColumns(driver, yyleng);
  return sysy::Parser::make_PLUS();
}

"-" {
  AdvanceColumns(driver, yyleng);
  return sysy::Parser::make_MINUS();
}

"*" {
  AdvanceColumns(driver, yyleng);
  return sysy::Parser::make_STAR();
}

"/" {
  AdvanceColumns(driver, yyleng);
  return sysy::Parser::make_SLASH();
}

"%" {
  AdvanceColumns(driver, yyleng);
  return sysy::Parser::make_PERCENT();
}

"!" {
  AdvanceColumns(driver, yyleng);
  return sysy::Parser::make_NOT();
}

"<" {
  AdvanceColumns(driver, yyleng);
  return sysy::Parser::make_LT();
}

">" {
  AdvanceColumns(driver, yyleng);
  return sysy::Parser::make_GT();
}

0[xX]{HEX_DIGIT}+ {
  int value = ParseInteger(yytext);
  AdvanceColumns(driver, yyleng);
  return sysy::Parser::make_INT_CONST(value);
}

{FLOAT_LIT} {
  double value = ParseFloat(yytext);
  AdvanceColumns(driver, yyleng);
  return sysy::Parser::make_FLOAT_CONST(value);
}

{DIGIT}+ {
  int value = ParseInteger(yytext);
  AdvanceColumns(driver, yyleng);
  return sysy::Parser::make_INT_CONST(value);
}

{IDENT_HEAD}{IDENT_TAIL}* {
  std::string name(yytext, yyleng);
  AdvanceColumns(driver, yyleng);
  return sysy::Parser::make_IDENT(name);
}

. {
  throw sysy::SemanticError(std::string("unexpected character: ") + yytext);
}

<<EOF>> {
  return sysy::Parser::make_YYEOF();
}

%%
