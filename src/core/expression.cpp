// ApoLite - small expression evaluator for If/Eval/inline expressions.
// Copyright (c) 2026 Joel Mandell. MIT licensed.
#include "apolite/expression.h"

#include <cctype>
#include <cmath>
#include <cstdlib>
#include <stdexcept>

namespace apolite {

namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kE = 2.71828182845904523536;

bool isIdentStart(char c) { return std::isalpha(static_cast<unsigned char>(c)) || c == '_'; }
bool isIdentChar(char c) {
    return std::isalnum(static_cast<unsigned char>(c)) || c == '_';
}

std::string toLower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

}  // namespace

bool Value::truthy() const {
    switch (type) {
        case Type::Number: return num != 0.0;
        case Type::String: return !str.empty();
        case Type::Bool: return boolean;
        case Type::Array: return !array.empty();
    }
    return false;
}

double Value::asNumber() const {
    switch (type) {
        case Type::Number: return num;
        case Type::Bool: return boolean ? 1.0 : 0.0;
        case Type::String: return std::strtod(str.c_str(), nullptr);
        case Type::Array: return array.empty() ? 0.0 : array.front().asNumber();
    }
    return 0.0;
}

std::string Value::asString() const {
    switch (type) {
        case Type::String: return str;
        case Type::Bool: return boolean ? "1" : "0";
        case Type::Number: {
            char buf[64];
            std::snprintf(buf, sizeof(buf), "%g", num);
            return buf;
        }
        case Type::Array: {
            std::string s;
            for (size_t i = 0; i < array.size(); ++i) {
                if (i) s += ",";
                s += array[i].asString();
            }
            return s;
        }
    }
    return {};
}

namespace {

class Parser {
public:
    Parser(const std::string& src, const ExpressionEvaluator& eval) : s_(src), eval_(eval) {}

    Value parse() {
        skipSpace();
        Value v = ternary();
        skipSpace();
        if (pos_ != s_.size()) throw std::runtime_error("unexpected trailing input");
        return v;
    }

private:
    const std::string& s_;
    const ExpressionEvaluator& eval_;
    size_t pos_ = 0;

    void skipSpace() {
        while (pos_ < s_.size() && std::isspace(static_cast<unsigned char>(s_[pos_]))) ++pos_;
    }

    bool consume(const std::string& tok) {
        skipSpace();
        if (s_.compare(pos_, tok.size(), tok) == 0) {
            pos_ += tok.size();
            return true;
        }
        return false;
    }

    bool consumeWord(const std::string& word) {
        skipSpace();
        const size_t n = word.size();
        if (pos_ + n > s_.size()) return false;
        if (toLower(s_.substr(pos_, n)) != word) return false;
        const size_t after = pos_ + n;
        if (after < s_.size() && isIdentChar(s_[after])) return false;
        pos_ = after;
        return true;
    }

    char peek() {
        skipSpace();
        return pos_ < s_.size() ? s_[pos_] : '\0';
    }

    Value ternary() {
        Value cond = logicalOr();
        skipSpace();
        if (consume("?")) {
            Value thenV = ternary();
            if (!consume(":")) throw std::runtime_error("expected ':'");
            Value elseV = ternary();
            return cond.truthy() ? thenV : elseV;
        }
        return cond;
    }

    Value logicalOr() {
        Value a = logicalAnd();
        for (;;) {
            skipSpace();
            if (consumeWord("or")) {
                a = Value::boolean_(a.truthy() || logicalAnd().truthy());
            } else if (peek() == '|' && !(pos_ + 1 < s_.size() && s_[pos_ + 1] == '|')) {
                ++pos_;
                a = bitwiseOr(a, logicalAnd());
            } else {
                return a;
            }
        }
    }

    Value logicalAnd() {
        Value a = comparison();
        for (;;) {
            skipSpace();
            if (consumeWord("and")) {
                a = Value::boolean_(a.truthy() && comparison().truthy());
            } else if (consumeWord("xor")) {
                a = Value::boolean_(a.truthy() != comparison().truthy());
            } else if (peek() == '&' && !(pos_ + 1 < s_.size() && s_[pos_ + 1] == '&')) {
                ++pos_;
                a = bitwiseAnd(a, comparison());
            } else {
                return a;
            }
        }
    }

    Value comparison() {
        Value a = shift();
        skipSpace();
        static const char* ops[] = {"==", "!=", "<=", ">=", "<", ">"};
        for (const char* op : ops) {
            if (consume(op)) {
                Value b = shift();
                return compare(a, b, op);
            }
        }
        return a;
    }

    Value shift() {
        Value a = additive();
        for (;;) {
            skipSpace();
            if (consume("<<")) {
                a = Value::number(static_cast<double>(static_cast<long long>(a.asNumber())
                                                      << static_cast<long long>(additive().asNumber())));
            } else if (consume(">>")) {
                a = Value::number(static_cast<double>(static_cast<long long>(a.asNumber())
                                                      >> static_cast<long long>(additive().asNumber())));
            } else {
                return a;
            }
        }
    }

    Value additive() {
        Value a = multiplicative();
        for (;;) {
            skipSpace();
            if (consume("+")) {
                Value b = multiplicative();
                if (a.isString() || b.isString())
                    a = Value::string(a.asString() + b.asString());
                else
                    a = Value::number(a.asNumber() + b.asNumber());
            } else if (peek() == '-' && pos_ + 1 < s_.size()) {
                ++pos_;
                a = Value::number(a.asNumber() - multiplicative().asNumber());
            } else {
                return a;
            }
        }
    }

    Value multiplicative() {
        Value a = power();
        for (;;) {
            skipSpace();
            if (consume("*")) {
                a = Value::number(a.asNumber() * power().asNumber());
            } else if (consume("/")) {
                a = Value::number(a.asNumber() / power().asNumber());
            } else {
                return a;
            }
        }
    }

    Value power() {
        Value a = unary();
        skipSpace();
        if (consume("^")) {
            Value b = power();
            a = Value::number(std::pow(a.asNumber(), b.asNumber()));
        }
        return a;
    }

    Value unary() {
        skipSpace();
        if (consumeWord("not")) return Value::boolean_(!unary().truthy());
        if (consume("-")) return Value::number(-unary().asNumber());
        if (consume("+")) return unary();
        if (consume("!")) return Value::boolean_(!unary().truthy());
        return postfix();
    }

    Value postfix() {
        Value a = primary();
        for (;;) {
            skipSpace();
            if (peek() == '[') {
                ++pos_;
                Value idx = ternary();
                if (!consume("]")) throw std::runtime_error("expected ']'");
                const size_t i = static_cast<size_t>(idx.asNumber());
                if (a.type == Value::Type::Array && i < a.array.size()) a = a.array[i];
                else a = Value::string("");
            } else {
                return a;
            }
        }
    }

    Value primary() {
        skipSpace();
        if (pos_ >= s_.size()) throw std::runtime_error("unexpected end of expression");
        const char c = s_[pos_];

        if (c == '(') {
            ++pos_;
            skipSpace();
            // Type conversion casts: (float), (int).
            if (toLower(s_.substr(pos_, 6)) == "float)") {
                pos_ += 6;
                return Value::number(unary().asNumber());
            }
            if (toLower(s_.substr(pos_, 4)) == "int)") {
                pos_ += 4;
                return Value::number(std::trunc(unary().asNumber()));
            }
            Value v = ternary();
            if (!consume(")")) throw std::runtime_error("expected ')'");
            return v;
        }
        if (c == '{') {
            ++pos_;
            Value arr;
            arr.type = Value::Type::Array;
            skipSpace();
            if (peek() == '}') {
                ++pos_;
                return arr;
            }
            for (;;) {
                arr.array.push_back(ternary());
                skipSpace();
                if (consume(",")) continue;
                if (consume("}")) break;
                throw std::runtime_error("expected ',' or '}'");
            }
            return arr;
        }
        if (c == '"' || c == '\'') {
            const char quote = c;
            ++pos_;
            std::string out;
            while (pos_ < s_.size() && s_[pos_] != quote) {
                if (s_[pos_] == '\\' && pos_ + 1 < s_.size()) {
                    ++pos_;
                    out += s_[pos_];
                } else {
                    out += s_[pos_];
                }
                ++pos_;
            }
            if (pos_ >= s_.size()) throw std::runtime_error("unterminated string");
            ++pos_;
            return Value::string(out);
        }
        if (std::isdigit(static_cast<unsigned char>(c)) || c == '.') {
            const char* start = s_.c_str() + pos_;
            char* end = nullptr;
            const double v = std::strtod(start, &end);
            pos_ += static_cast<size_t>(end - start);
            return Value::number(v);
        }
        if (isIdentStart(c)) {
            std::string ident;
            while (pos_ < s_.size() && isIdentChar(s_[pos_])) ident += s_[pos_++];
            skipSpace();
            if (peek() == '(') return functionCall(ident);
            return identifier(ident);
        }
        throw std::runtime_error("unexpected character");
    }

    Value identifier(const std::string& name) {
        if (name == "pi") return Value::number(kPi);
        if (name == "e") return Value::number(kE);
        if (name == "true") return Value::boolean_(true);
        if (name == "false") return Value::boolean_(false);
        if (const Value* v = eval_.getVariable(name)) return *v;
        throw std::runtime_error("unknown identifier: " + name);
    }

    Value functionCall(const std::string& name) {
        if (!consume("(")) throw std::runtime_error("expected '('");
        std::vector<Value> args;
        skipSpace();
        if (peek() != ')') {
            for (;;) {
                args.push_back(ternary());
                skipSpace();
                if (consume(",")) continue;
                break;
            }
        }
        if (!consume(")")) throw std::runtime_error("expected ')'");

        const std::string fn = toLower(name);
        auto arg = [&](size_t i) { return i < args.size() ? args[i].asNumber() : 0.0; };

        if (fn == "abs") return Value::number(std::fabs(arg(0)));
        if (fn == "sin") return Value::number(std::sin(arg(0)));
        if (fn == "cos") return Value::number(std::cos(arg(0)));
        if (fn == "tan") return Value::number(std::tan(arg(0)));
        if (fn == "sinh") return Value::number(std::sinh(arg(0)));
        if (fn == "cosh") return Value::number(std::cosh(arg(0)));
        if (fn == "tanh") return Value::number(std::tanh(arg(0)));
        if (fn == "asin") return Value::number(std::asin(arg(0)));
        if (fn == "acos") return Value::number(std::acos(arg(0)));
        if (fn == "atan") return Value::number(std::atan(arg(0)));
        if (fn == "ln") return Value::number(std::log(arg(0)));
        if (fn == "log") return Value::number(std::log(arg(0)));
        if (fn == "log10") return Value::number(std::log10(arg(0)));
        if (fn == "exp") return Value::number(std::exp(arg(0)));
        if (fn == "sqrt") return Value::number(std::sqrt(arg(0)));
        if (fn == "floor") return Value::number(std::floor(arg(0)));
        if (fn == "ceil") return Value::number(std::ceil(arg(0)));
        if (fn == "round") return Value::number(std::round(arg(0)));
        if (fn == "str2dbl") return Value::number(std::strtod(args[0].asString().c_str(), nullptr));
        if (fn == "strlen") return Value::number(static_cast<double>(args[0].asString().size()));
        if (fn == "tolower") return Value::string(toLower(args[0].asString()));
        if (fn == "toupper") {
            std::string v = args[0].asString();
            for (char& ch : v) ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
            return Value::string(v);
        }
        if (fn == "sizeof") {
            if (!args.empty() && args[0].type == Value::Type::Array)
                return Value::number(static_cast<double>(args[0].array.size()));
            return Value::number(static_cast<double>(args[0].asString().size()));
        }
        if (fn == "min") {
            double m = arg(0);
            for (size_t i = 1; i < args.size(); ++i) m = std::min(m, arg(i));
            return Value::number(m);
        }
        if (fn == "max") {
            double m = arg(0);
            for (size_t i = 1; i < args.size(); ++i) m = std::max(m, arg(i));
            return Value::number(m);
        }
        if (fn == "sum") {
            double m = 0.0;
            for (size_t i = 0; i < args.size(); ++i) m += arg(i);
            return Value::number(m);
        }
        throw std::runtime_error("unknown function: " + name);
    }

    static Value bitwiseOr(const Value& a, const Value& b) {
        return Value::number(static_cast<double>(static_cast<long long>(a.asNumber()) |
                                                 static_cast<long long>(b.asNumber())));
    }
    static Value bitwiseAnd(const Value& a, const Value& b) {
        return Value::number(static_cast<double>(static_cast<long long>(a.asNumber()) &
                                                 static_cast<long long>(b.asNumber())));
    }

    static Value compare(const Value& a, const Value& b, const std::string& op) {
        if (a.isString() || b.isString()) {
            const std::string x = a.asString(), y = b.asString();
            if (op == "==") return Value::boolean_(x == y);
            if (op == "!=") return Value::boolean_(x != y);
            if (op == "<") return Value::boolean_(x < y);
            if (op == ">") return Value::boolean_(x > y);
            if (op == "<=") return Value::boolean_(x <= y);
            if (op == ">=") return Value::boolean_(x >= y);
        }
        const double x = a.asNumber(), y = b.asNumber();
        if (op == "==") return Value::boolean_(x == y);
        if (op == "!=") return Value::boolean_(x != y);
        if (op == "<") return Value::boolean_(x < y);
        if (op == ">") return Value::boolean_(x > y);
        if (op == "<=") return Value::boolean_(x <= y);
        if (op == ">=") return Value::boolean_(x >= y);
        return Value::boolean_(false);
    }
};

}  // namespace

ExpressionEvaluator::ExpressionEvaluator() = default;

void ExpressionEvaluator::setVariable(const std::string& name, Value v) {
    variables_[name] = std::move(v);
}

bool ExpressionEvaluator::hasVariable(const std::string& name) const {
    return variables_.find(name) != variables_.end();
}

const Value* ExpressionEvaluator::getVariable(const std::string& name) const {
    const auto it = variables_.find(name);
    return it == variables_.end() ? nullptr : &it->second;
}

Value ExpressionEvaluator::evaluate(const std::string& expr) const {
    Parser p(expr, *this);
    return p.parse();
}

}  // namespace apolite
