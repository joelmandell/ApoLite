// ApoLite - small expression evaluator for If/Eval/inline expressions.
// Copyright (c) 2026 Joel Mandell. MIT licensed.
#pragma once

#include <map>
#include <string>
#include <vector>

namespace apolite {

// A dynamically typed value: number, string, bool, or array.
struct Value {
    enum class Type { Number, String, Bool, Array };

    Type type = Type::Number;
    double num = 0.0;
    std::string str;
    bool boolean = false;
    std::vector<Value> array;

    static Value number(double v) {
        Value x;
        x.type = Type::Number;
        x.num = v;
        return x;
    }
    static Value string(std::string s) {
        Value x;
        x.type = Type::String;
        x.str = std::move(s);
        return x;
    }
    static Value boolean_(bool b) {
        Value x;
        x.type = Type::Bool;
        x.boolean = b;
        return x;
    }

    bool isString() const { return type == Type::String; }

    // Truthiness: nonzero number, non-empty string, true bool.
    bool truthy() const;

    double asNumber() const;
    std::string asString() const;
};

// Evaluates expressions in the Equalizer APO configuration language.
class ExpressionEvaluator {
public:
    ExpressionEvaluator();

    void setVariable(const std::string& name, Value v);
    bool hasVariable(const std::string& name) const;
    const Value* getVariable(const std::string& name) const;

    // Evaluate `expr`. Throws std::runtime_error on a syntax error.
    Value evaluate(const std::string& expr) const;

private:
    std::map<std::string, Value> variables_;
};

}  // namespace apolite
