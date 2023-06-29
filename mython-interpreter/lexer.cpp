#include "lexer.h"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <unordered_map>

using namespace std;

namespace parse {

bool operator==(const Token& lhs, const Token& rhs) {
    using namespace token_type;

    if (lhs.index() != rhs.index()) {
        return false;
    }
    if (lhs.Is<Char>()) {
        return lhs.As<Char>().value == rhs.As<Char>().value;
    }
    if (lhs.Is<Number>()) {
        return lhs.As<Number>().value == rhs.As<Number>().value;
    }
    if (lhs.Is<String>()) {
        return lhs.As<String>().value == rhs.As<String>().value;
    }
    if (lhs.Is<Id>()) {
        return lhs.As<Id>().value == rhs.As<Id>().value;
    }
    return true;
}

bool operator!=(const Token& lhs, const Token& rhs) {
    return !(lhs == rhs);
}

std::ostream& operator<<(std::ostream& os, const Token& rhs) {
    using namespace token_type;

#define VALUED_OUTPUT(type) \
    if (auto p = rhs.TryAs<type>()) return os << #type << '{' << p->value << '}';

    VALUED_OUTPUT(Number);
    VALUED_OUTPUT(Id);
    VALUED_OUTPUT(String);
    VALUED_OUTPUT(Char);

#undef VALUED_OUTPUT

#define UNVALUED_OUTPUT(type) \
    if (rhs.Is<type>()) return os << #type;

    UNVALUED_OUTPUT(Class);
    UNVALUED_OUTPUT(Return);
    UNVALUED_OUTPUT(If);
    UNVALUED_OUTPUT(Else);
    UNVALUED_OUTPUT(Def);
    UNVALUED_OUTPUT(Newline);
    UNVALUED_OUTPUT(Print);
    UNVALUED_OUTPUT(Indent);
    UNVALUED_OUTPUT(Dedent);
    UNVALUED_OUTPUT(And);
    UNVALUED_OUTPUT(Or);
    UNVALUED_OUTPUT(Not);
    UNVALUED_OUTPUT(Eq);
    UNVALUED_OUTPUT(NotEq);
    UNVALUED_OUTPUT(LessOrEq);
    UNVALUED_OUTPUT(GreaterOrEq);
    UNVALUED_OUTPUT(None);
    UNVALUED_OUTPUT(True);
    UNVALUED_OUTPUT(False);
    UNVALUED_OUTPUT(Eof);

#undef UNVALUED_OUTPUT

    return os << "Unknown token :("sv;
}


Lexer::Lexer(std::istream& input)
    : input_(input)
    , current_token_str_{}
    , current_ws_indent_{0}
    , new_ws_indent_{0}
    , state_{State::NEW_LINE} {
    InitTokenQueue();
}

const Token& Lexer::CurrentToken() const {
    return tokens_buf_.front();
}

Token Lexer::NextToken() {
    for (; input_ && !input_.eof();) {
        if (!FeedChar(input_.get())) {
            break;
        }
    }
    if (!input_ || input_.eof()) {
        if (current_ws_indent_ > 0) {
            PutIndentDedent(token_type::Dedent{}, current_ws_indent_ / 2);
            current_ws_indent_ = 0;
        }
        tokens_buf_.push_back(token_type::Eof{});
    }
    if (tokens_buf_.size() > 1) {
        tokens_buf_.pop_front();
    }
    return tokens_buf_.front();
}

bool Lexer::FeedChar(char c) {
    int const row = (state_ == State::NEW_LINE) ? 0 :
                        (state_ == State::MAYBE_ID) ? 1 :
                        (state_ == State::MAYBE_COMPARE) ? 2 :
                        (state_ == State::NUMBER) ? 3 :
                        (state_ == State::STRING_SQ) ? 4 :
                        (state_ == State::SQ_ESCAPE) ? 5 :
                        (state_ == State::STRING_DQ) ? 6 :
                        (state_ == State::DQ_ESCAPE) ? 7 :
                        (state_ == State::TRAILING_COMMENT) ? 8 :
                        (state_ == State::LINE_COMMENT) ? 9 : 10;

    int const column = (c == '\n') ? 0 :
                       std::isspace(c) ? 1 :
                       (c == '_' || std::isalpha(c)) ? 2 :
                       std::isdigit(c) ? 3 :
                       (c == '\'') ? 4 :
                       (c == '"') ? 5 :
                       (c == '#') ? 6 :
                       (c == '\\') ? 7 :
                       (c == '=' || c == '<' || c == '>' || c == '!') ? 8 :
                       (c == std::char_traits<char>::eof()) ? 9 : 10;

    const Branch* const b = &transitions_[row][column];
    state_ = b->next_state;
    return b->action(this, c);
}

void Lexer::InitTokenQueue() {
    for (; input_ && !input_.eof();) {
        if (!FeedChar(input_.get())) {
            break;
        }
    }
    if (tokens_buf_.empty()) {
        tokens_buf_.push_back(token_type::Eof{});
    }
}

// -- automata actions --

bool Lexer::Error(Lexer*, char) {
    throw LexerError("Invalid Lexeme");
    return false;
}

bool Lexer::Nop(Lexer*, char) {
    return true;
}

bool Lexer::CountWS(Lexer* l, char) {
    ++(l->new_ws_indent_);
    return true;
}

bool Lexer::DropIndent(Lexer* l, char) {
//    l->new_ws_indent_ = l->current_ws_indent_;
    l->new_ws_indent_ = 0;
    return true;
}

bool Lexer::PutNLToken(Lexer* l, char) {
    l->tokens_buf_.push_back(token_type::Newline{});
    return false;
}

bool Lexer::PutIndentToken(Lexer* l, char) {
    using namespace std::literals;
    if (l->new_ws_indent_ % INDENT_SIZE != 0) {
        throw LexerError("Invalid Indentation"s);
    }
    if (l->new_ws_indent_ > l->current_ws_indent_) {
        l->PutIndentDedent(token_type::Indent{}, (l->new_ws_indent_ - l->current_ws_indent_) / INDENT_SIZE);
    }
    if (l->current_ws_indent_ > l->new_ws_indent_) {
        l->PutIndentDedent(token_type::Dedent{}, (l->current_ws_indent_ - l->new_ws_indent_) / INDENT_SIZE);
    }
    l->current_ws_indent_ = l->new_ws_indent_;
    l->new_ws_indent_ = 0;
    return true;
}

bool Lexer::PutIdToken(Lexer* l, char) {
    using namespace std::literals;
    if (l->current_token_str_ == "and"s) {
        l->tokens_buf_.push_back(token_type::And{});
    } else if (l->current_token_str_ == "class"s) {
        l->tokens_buf_.push_back(token_type::Class{});
    } else if (l->current_token_str_ == "def"s) {
        l->tokens_buf_.push_back(token_type::Def{});
    } else if (l->current_token_str_ == "else"s) {
        l->tokens_buf_.push_back(token_type::Else{});
    } else if (l->current_token_str_ == "False"s) {
        l->tokens_buf_.push_back(token_type::False{});
    } else if (l->current_token_str_ == "if"s) {
        l->tokens_buf_.push_back(token_type::If{});
    } else if (l->current_token_str_ == "None"s) {
        l->tokens_buf_.push_back(token_type::None{});
    } else if (l->current_token_str_ == "not"s) {
        l->tokens_buf_.push_back(token_type::Not{});
    } else if (l->current_token_str_ == "or"s) {
        l->tokens_buf_.push_back(token_type::Or{});
    } else if (l->current_token_str_ == "print"s) {
        l->tokens_buf_.push_back(token_type::Print{});
    } else if (l->current_token_str_ == "return"s) {
        l->tokens_buf_.push_back(token_type::Return{});
    } else if (l->current_token_str_ == "True"s) {
        l->tokens_buf_.push_back(token_type::True{});
    } else {
        l->tokens_buf_.push_back(token_type::Id{std::move(l->current_token_str_)});
    }
    l->current_token_str_.clear();
    return false;
}

bool Lexer::PutCharToken(Lexer* l, char c) {
    l->tokens_buf_.push_back(token_type::Char{c});
    return false;
}

bool Lexer::PutBufCharToken(Lexer* l, char) {
    l->tokens_buf_.push_back(token_type::Char{l->current_token_str_[0]});
    l->current_token_str_.clear();
    return false;
}

bool Lexer::PutCompToken(Lexer* l, char c) {
    const auto b = l->current_token_str_[0];
    if (c == '=') {
        if (b == '=') {
            l->tokens_buf_.push_back(token_type::Eq{});
        } else if (b == '!') {
            l->tokens_buf_.push_back(token_type::NotEq{});
        } else if (b == '<') {
            l->tokens_buf_.push_back(token_type::LessOrEq{});
        } else { // '>'
            l->tokens_buf_.push_back(token_type::GreaterOrEq{});
        }
    } else {
        PutCharToken(l, b);
        PutCharToken(l, c);
    }
    l->current_token_str_.clear();
    return false;
}

bool Lexer::PutNumberToken(Lexer* l, char) {
    int num = 0;
    char* first = l->current_token_str_.data();
    char* last = first + l->current_token_str_.size();
    std::from_chars(first, last, num);
    l->tokens_buf_.push_back(token_type::Number{num});
    l->current_token_str_.clear();
    return false;
}

bool Lexer::PutStringToken(Lexer* l, char) {
    l->tokens_buf_.push_back(token_type::String{std::move(l->current_token_str_)});
    l->current_token_str_.clear();
    return false;
}

// -- composed actions --

bool Lexer::PutIdAndNLTokens(Lexer* l, char) {
    PutIdToken(l, ' ');
    PutNLToken(l, ' ');
    return false;
}

bool Lexer::PutBufCharAndNLTokens(Lexer* l, char) {
    PutBufCharToken(l, ' ');
    PutNLToken(l, ' ');
    return false;
}

bool Lexer::PutCompAndNLTokens(Lexer* l, char) {
    PutCompToken(l, ' ');
    PutNLToken(l, ' ');
    return false;
}

bool Lexer::PutNumberAndNLTokens(Lexer* l, char) {
    PutNumberToken(l, ' ');
    PutNLToken(l, ' ');
    return false;
}

bool Lexer::PutIndentAndCharTokens(Lexer* l, char c) {
    PutIndentToken(l, ' ');
    PutCharToken(l, c)    ;
    return false;
}

bool Lexer::PutIdAndCharTokens(Lexer* l, char c) {
    PutIdToken(l, ' ');
    PutCharToken(l, c);
    return false;
}
bool Lexer::PutNumberAndCharTokens(Lexer* l, char c) {
    PutNumberToken(l, ' ');
    PutCharToken(l, c);
    return false;
}
bool Lexer::PutBufCharAndCharTokens(Lexer* l, char c) {
    PutBufCharToken(l, ' ');
    PutCharToken(l, c);
    return false;
}

// -- composed actions - PutChar family --

bool Lexer::PutChar(Lexer* l, char c) {
    l->current_token_str_ += c;
    return true;
}

bool Lexer::PutControlChar(Lexer* l, char c) {
    if (c == 'n') {
        c = '\n';
    }
    if (c == 't') {
        c = '\t';
    }
    PutChar(l, c);
    return true;
}

bool Lexer::PutIndentTokenAndChar(Lexer* l, char c) {
    PutIndentToken(l, ' ');
    PutChar(l, c);
    return true;
}

bool Lexer::PutIdTokenAndChar(Lexer* l, char c) {
    PutIdToken(l, ' ');
    PutChar(l, c);
    return false;
}

bool Lexer::PutCompTokenAndChar(Lexer* l, char c) {
    PutCompToken(l, ' ');
    PutChar(l, c);
    return false;
}

bool Lexer::PutNumberTokenAndChar(Lexer* l, char c) {
    PutNumberToken(l, ' ');
    PutChar(l, c);
    return false;
}

Branch Lexer::transitions_[11][11] = {
    /* newline                                whitespace                           underscore&alpha                           digit                                  sq (') mark                         dq (") mark                         comment (#)                                backslash (\)                                   maybe compare (=<>!)                        eof                                        other chars   */
    {{State::NEW_LINE, DropIndent},           {State::NEW_LINE, CountWS},          {State::MAYBE_ID, PutIndentTokenAndChar}, {State::NUMBER, PutIndentTokenAndChar}, {State::STRING_SQ, PutIndentToken}, {State::STRING_DQ, PutIndentToken},  {State::LINE_COMMENT, Nop},                {State::OUT_STATE, PutIndentAndCharTokens}, {State::MAYBE_COMPARE, PutIndentTokenAndChar}, {State::OUT_STATE, DropIndent},            {State::OUT_STATE, PutIndentAndCharTokens}}, // NEWLINE
    {{State::NEW_LINE, PutIdAndNLTokens},     {State::OUT_STATE, PutIdToken},      {State::MAYBE_ID, PutChar},               {State::MAYBE_ID, PutChar},             {State::STRING_SQ, PutIdToken},     {State::STRING_DQ, PutIdToken},      {State::TRAILING_COMMENT, PutIdToken},     {State::OUT_STATE, PutIdAndCharTokens},     {State::MAYBE_COMPARE, PutIdTokenAndChar},     {State::OUT_STATE, PutIdAndNLTokens},      {State::OUT_STATE, PutIdAndCharTokens}},     // MAYBE_ID
    {{State::NEW_LINE, PutCompAndNLTokens},   {State::OUT_STATE, PutBufCharToken}, {State::MAYBE_ID, PutCompTokenAndChar},   {State::NUMBER, PutCompTokenAndChar},   {State::STRING_SQ, PutCompToken},   {State::STRING_DQ, PutCompToken},    {State::TRAILING_COMMENT, PutCompToken},   {State::OUT_STATE, PutBufCharAndCharTokens},{State::OUT_STATE, PutCompToken},              {State::OUT_STATE, PutBufCharAndNLTokens}, {State::OUT_STATE, PutBufCharAndCharTokens}},// MAYBE_COMPARE
    {{State::NEW_LINE, PutNumberAndNLTokens}, {State::OUT_STATE, PutNumberToken},  {State::MAYBE_ID, PutNumberTokenAndChar}, {State::NUMBER, PutChar},               {State::STRING_SQ, PutNumberToken}, {State::STRING_DQ, PutNumberToken},  {State::TRAILING_COMMENT, PutNumberToken}, {State::OUT_STATE, PutNumberAndCharTokens}, {State::MAYBE_COMPARE, PutNumberTokenAndChar}, {State::OUT_STATE, PutNumberAndNLTokens},  {State::OUT_STATE, PutNumberAndCharTokens}}, // NUMBER
    {{State::OUT_STATE, Error},               {State::STRING_SQ, PutChar},         {State::STRING_SQ, PutChar},              {State::STRING_SQ, PutChar},            {State::OUT_STATE, PutStringToken}, {State::STRING_SQ, PutChar},         {State::STRING_SQ, PutChar},               {State::SQ_ESCAPE, Nop},                    {State::STRING_SQ, PutChar},                   {State::OUT_STATE, Error},                 {State::STRING_SQ, PutChar}},                // STRING_SQ
    {{State::OUT_STATE, Error},               {State::STRING_SQ, PutChar},         {State::STRING_SQ, PutControlChar},       {State::STRING_SQ, PutChar},            {State::STRING_SQ, PutChar},        {State::STRING_SQ, PutChar},         {State::STRING_SQ, PutChar},               {State::STRING_SQ, PutChar},                {State::STRING_SQ, PutChar},                   {State::OUT_STATE, Error},                 {State::STRING_SQ, PutChar}},                // SQ_ESCAPE
    {{State::OUT_STATE, Error},               {State::STRING_DQ, PutChar},         {State::STRING_DQ, PutChar},              {State::STRING_DQ, PutChar},            {State::STRING_DQ, PutChar},        {State::OUT_STATE, PutStringToken},  {State::STRING_DQ, PutChar},               {State::DQ_ESCAPE, Nop},                    {State::STRING_DQ, PutChar},                   {State::OUT_STATE, Error},                 {State::STRING_DQ, PutChar}},                // STRING_DQ
    {{State::OUT_STATE, Error},               {State::STRING_DQ, PutChar},         {State::STRING_DQ, PutControlChar},       {State::STRING_DQ, PutChar},            {State::STRING_DQ, PutChar},        {State::STRING_DQ, PutChar},         {State::STRING_DQ, PutChar},               {State::STRING_DQ, PutChar},                {State::STRING_DQ, PutChar},                   {State::OUT_STATE, Error},                 {State::STRING_DQ, PutChar}},                // DQ_ESCAPE
    {{State::NEW_LINE, PutNLToken},           {State::TRAILING_COMMENT, Nop},      {State::TRAILING_COMMENT, Nop},           {State::TRAILING_COMMENT, Nop},         {State::TRAILING_COMMENT, Nop},     {State::TRAILING_COMMENT, Nop},      {State::TRAILING_COMMENT, Nop},            {State::TRAILING_COMMENT, Nop},             {State::TRAILING_COMMENT, Nop},                {State::OUT_STATE, PutNLToken},            {State::TRAILING_COMMENT, Nop}},             // TRAILING_COMMENT
    {{State::NEW_LINE, Nop},                  {State::LINE_COMMENT, Nop},          {State::LINE_COMMENT, Nop},               {State::LINE_COMMENT, Nop},             {State::LINE_COMMENT, Nop},         {State::LINE_COMMENT, Nop},          {State::LINE_COMMENT, Nop},                {State::LINE_COMMENT, Nop},                 {State::LINE_COMMENT, Nop},                    {State::OUT_STATE, Nop},                   {State::LINE_COMMENT, Nop}},                 // LINE_COMMENT
    {{State::NEW_LINE, PutNLToken},           {State::OUT_STATE, Nop},             {State::MAYBE_ID, PutChar},               {State::NUMBER, PutChar},               {State::STRING_SQ, Nop},            {State::STRING_DQ, Nop},             {State::TRAILING_COMMENT, Nop},            {State::OUT_STATE, PutCharToken},           {State::MAYBE_COMPARE, PutChar},               {State::OUT_STATE, PutNLToken},            {State::OUT_STATE, PutCharToken}}            // OUT_STATE
};

}  // namespace parse
