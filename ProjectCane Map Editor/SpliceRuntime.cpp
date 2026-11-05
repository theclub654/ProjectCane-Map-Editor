#include "SpliceRuntime.h"
#include "EditorOptions.h"

#include <algorithm>
#include <charconv>
#include <cctype>
#include <climits>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <iomanip>
#include <iterator>
#include <sstream>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace
{
constexpr std::uint8_t kSet = 1, kDefine = 2, kAssert = 5, kIf = 6, kOr = 7, kAnd = 8, kCond = 9, kElse = 10,
                       kCase = 11, kLet = 12, kWhile = 13, kLambda = 14, kBegin = 15, kImport = 16, kQuote = 17,
                       kS32 = 20, kF32 = 21, kVector = 22, kMatrix = 23, kClq = 24, kLm = 25, kSmp = 26, kBool = 27,
                       kSymid = 28, kBifk = 29, kPair = 30, kProc = 31, kBasic = 32, kMethod = 33, kVoid = 34,
                       kErr = 35;

// Exact crefReq/fVarArg metadata from ProjectCane's g_mpbifkbif table.  Keep
// source validation aligned with RefEvalApply instead of validating only the
// handful of built-ins used by friendly templates.
constexpr std::uint8_t kBuiltinRequired[125] = {
    1, 1, 1, 1, 1, 0, 1, 2, 2, 2, 2, 2, 2, 2, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 3, 1,
    2, 1, 1, 2, 2, 1, 2, 2, 0, 0, 2, 2, 2, 1, 3, 0, 3, 2, 3, 1, 1, 2, 1, 1, 1, 1, 1, 1, 1, 1, 2, 2,
    2, 1, 1, 1, 1, 2, 2, 3, 2, 1, 1, 2, 3, 1, 1, 1, 1, 2, 1, 2, 3, 4, 2, 2, 1, 1, 1, 1, 1, 1, 1, 2,
    0, 2, 2, 3, 3, 3, 2, 2, 2, 2, 2, 0, 0, 0, 2, 2, 2, 3, 3, 3, 3, 3, 0, 2, 1, 2, 2, 7, 2};
constexpr std::uint8_t kBuiltinVariadic[125] = {
    1, 1, 1, 1, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 0, 0, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 1, 1, 1, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 0,
    0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};

constexpr std::size_t kMaximumSpliceNodes = 10'000'000u; // BrxLoader's expression-table limit.

bool CanAppendSpliceNodes(const BrxDocument &document, const std::size_t count)
{
    return count <= kMaximumSpliceNodes && document.SpliceNodes().size() <= kMaximumSpliceNodes - count;
}

bool ValidNode(const BrxDocument &document, const std::int32_t node)
{
    return node >= 0 && static_cast<std::size_t>(node) < document.SpliceNodes().size();
}

bool IsLambdaExpression(const BrxDocument &document, const std::int32_t node)
{
    if (!ValidNode(document, node))
        return false;
    const auto &expression = document.SpliceNodes()[static_cast<std::size_t>(node)];
    return expression.tag == kPair && ValidNode(document, expression.childNode) &&
           document.SpliceNodes()[static_cast<std::size_t>(expression.childNode)].tag == kLambda;
}

bool IsRunnableEventLambda(const BrxDocument &document, const std::int32_t node)
{
    if (!IsLambdaExpression(document, node))
        return false;
    const auto lambda = document.SpliceNodes()[static_cast<std::size_t>(node)].childNode;
    const auto formals = document.SpliceNodes()[static_cast<std::size_t>(lambda)].nextNode;
    if (!ValidNode(document, formals))
        return false;
    const auto &formalsNode = document.SpliceNodes()[static_cast<std::size_t>(formals)];
    if (formalsNode.tag != kPair || !ValidNode(document, formalsNode.childNode) ||
        document.SpliceNodes()[static_cast<std::size_t>(formalsNode.childNode)].tag != kSymid)
        return false;
    return ValidNode(document, formalsNode.nextNode);
}

std::vector<std::int32_t> SpliceInitializationExpressions(const std::vector<BrxSpliceNode> &nodes,
                                                         const std::int32_t root)
{
    std::vector<std::int32_t> expressions;
    std::vector<std::pair<std::int32_t, bool>> pending{{root, false}};
    std::unordered_set<std::int32_t> visited;
    const auto valid = [&](const std::int32_t index)
        { return index >= 0 && static_cast<std::size_t>(index) < nodes.size(); };
    while (!pending.empty())
    {
        const auto [index, siblings] = pending.back();
        pending.pop_back();
        if (!valid(index) || !visited.insert(index).second)
            continue;
        const auto &node = nodes[static_cast<std::size_t>(index)];
        if (siblings)
            pending.emplace_back(node.nextNode, true);
        if (node.tag == kPair && valid(node.childNode) &&
            nodes[static_cast<std::size_t>(node.childNode)].tag == kBegin)
            pending.emplace_back(nodes[static_cast<std::size_t>(node.childNode)].nextNode, true);
        else
            expressions.push_back(index);
    }
    return expressions;
}

struct SandboxValue
{
    enum class Kind
    {
        None,
        Nil,
        Int,
        Float,
        Vector,
        Matrix,
        Clq,
        Lm,
        Smp,
        Bool,
        Symbol,
        Object,
        Lambda,
        Procedure,
        Builtin,
        Method,
        Void,
        Error,
        Quoted
    } kind = Kind::None;
    std::int32_t integer = 0;
    float number = 0.0f;
    float components[16]{};
    std::int32_t node = -1;
    bool Truthy() const
    {
        return kind != Kind::Bool || integer != 0;
    }
};

struct Sandbox
{
    const BrxDocument &document;
    SpliceSandboxResult result;
    std::uint32_t limit = 10000;
    std::unordered_map<std::uint32_t, SandboxValue> bindings;
    std::uint32_t evaluationDepth = 0;
    std::uint32_t lambdaCallDepth = 0;
    bool halted = false;
    std::unordered_map<std::int32_t, std::unordered_map<std::uint32_t, SandboxValue>> moduleBindings;
    std::unordered_set<std::int32_t> loadingModules;
    std::uint32_t importedParentCount = 0;

    bool Running() const
    {
        return !halted && !result.limitReached;
    }

    void Approximation(const std::string &message)
    {
        result.approximated = true;
        if (std::find(result.diagnostics.begin(), result.diagnostics.end(), message) == result.diagnostics.end())
            result.diagnostics.push_back(message);
    }

    bool Eqv(const SandboxValue &a, const SandboxValue &b)
    {
        if (a.kind != b.kind)
            return false;
        switch (a.kind)
        {
        case SandboxValue::Kind::Int:
        case SandboxValue::Kind::Bool:
        case SandboxValue::Kind::Symbol:
        case SandboxValue::Kind::Object:
        case SandboxValue::Kind::Builtin:
            return a.integer == b.integer;
        case SandboxValue::Kind::None:
        case SandboxValue::Kind::Void:
        case SandboxValue::Kind::Procedure:
        case SandboxValue::Kind::Method:
            return true;
        case SandboxValue::Kind::Lambda:
            Approximation("Procedure identity is approximated by code identity; captured closure identity is not modeled.");
            return a.node == b.node;
        case SandboxValue::Kind::Quoted:
            if (ValidNode(document, a.node) && ValidNode(document, b.node))
            {
                const auto &left = document.SpliceNodes()[static_cast<std::size_t>(a.node)];
                const auto &right = document.SpliceNodes()[static_cast<std::size_t>(b.node)];
                if (left.tag != right.tag)
                    return false;
                if (left.tag == kPair)
                    return left.childNode == right.childNode;
                return left.tag <= 19u; // RefOpEqv treats matching keyword tags as equivalent.
            }
            return false;
        default:
            // Native Eqv has no numeric-compound, F32, Nil or Err case.
            return false;
        }
    }

    bool CheckExpressionGraph(const std::int32_t root)
    {
        std::unordered_map<std::int32_t, std::uint8_t> states;
        std::vector<std::pair<std::int32_t, bool>> pending{{root, false}};
        while (!pending.empty())
        {
            const auto [index, leaving] = pending.back();
            pending.pop_back();
            if (index == -1)
                continue;
            if (!ValidNode(document, index))
            {
                halted = true;
                result.diagnostics.push_back("Preview stopped: invalid expression link.");
                return false;
            }
            if (leaving)
            {
                states[index] = 2;
                continue;
            }
            if (states[index] == 1)
            {
                halted = true;
                result.diagnostics.push_back("Preview stopped: expression graph contains a child/next cycle.");
                return false;
            }
            if (states[index] == 2)
                continue;
            if (states.size() > 100'000u)
            {
                result.limitReached = true;
                result.diagnostics.push_back("Preview expression-graph inspection limit reached.");
                return false;
            }
            states[index] = 1;
            const auto &node = document.SpliceNodes()[static_cast<std::size_t>(index)];
            pending.emplace_back(index, true);
            pending.emplace_back(node.nextNode, false);
            pending.emplace_back(node.childNode, false);
        }
        return true;
    }

    struct ShadowedBinding
    {
        std::uint32_t symbol = 0;
        bool existed = false;
        SandboxValue value;
    };

    ShadowedBinding Shadow(const std::uint32_t symbol) const
    {
        const auto found = bindings.find(symbol);
        return {symbol, found != bindings.end(), found != bindings.end() ? found->second : SandboxValue{}};
    }

    void Restore(const std::vector<ShadowedBinding> &shadowed)
    {
        for (auto item = shadowed.rbegin(); item != shadowed.rend(); ++item)
        {
            if (item->existed)
                bindings[item->symbol] = item->value;
            else
                bindings.erase(item->symbol);
        }
    }

    void EvalModuleBody(const std::int32_t spliceIndex)
    {
        if (spliceIndex < 0 || static_cast<std::size_t>(spliceIndex) >= document.SpliceModules().size())
        {
            result.diagnostics.push_back("Import references missing splice " + std::to_string(spliceIndex) + ".");
            return;
        }
        // RefEvalModule evaluates one serialized root expression. Multiple
        // executable forms live beneath that root's (begin ...), not beside it.
        const auto root = document.SpliceModules()[spliceIndex].rootNode;
        if (Running() && CheckExpressionGraph(root))
            Eval(root);
    }

    const std::unordered_map<std::uint32_t, SandboxValue> *LoadImportedModule(const std::int32_t spliceIndex)
    {
        if (spliceIndex < 0 || static_cast<std::size_t>(spliceIndex) >= document.SpliceModules().size())
        {
            result.diagnostics.push_back("Import references missing splice " + std::to_string(spliceIndex) + ".");
            return nullptr;
        }
        // RefEvalModule does not allocate a frame for a null compiled root.
        if (document.SpliceModules()[static_cast<std::size_t>(spliceIndex)].rootNode == -1)
            return nullptr;
        if (const auto cached = moduleBindings.find(spliceIndex); cached != moduleBindings.end())
            return &cached->second;
        if (!loadingModules.insert(spliceIndex).second)
        {
            Approximation("Cyclic module frames are supported by ProjectCane but are not modeled exactly by this preview.");
            return nullptr;
        }

        auto callerBindings = std::move(bindings);
        const auto callerParentCount = importedParentCount;
        bindings.clear();
        importedParentCount = 0;
        EvalModuleBody(spliceIndex);
        auto importedBindings = std::move(bindings);
        bindings = std::move(callerBindings);
        importedParentCount = callerParentCount;
        loadingModules.erase(spliceIndex);
        return &moduleBindings.emplace(spliceIndex, std::move(importedBindings)).first->second;
    }

    void ImportModule(const std::int32_t spliceIndex)
    {
        Approximation("Imported lexical frames are flattened in this preview; captured bindings may differ from runtime.");
        if (importedParentCount >= 4)
        {
            result.diagnostics.push_back("Import of splice module " + std::to_string(spliceIndex) +
                                         " is ignored because ProjectCane frames support at most four parents.");
            return;
        }
        const auto *imported = LoadImportedModule(spliceIndex);
        if (imported == nullptr)
            return;
        ++importedParentCount;
        // CFrame::PrefFindBinding searches the current frame first and imported
        // parents in insertion order. try_emplace preserves both rules in the
        // sandbox's flattened, non-mutating preview environment.
        for (const auto &[symbol, value] : *imported)
            bindings.try_emplace(symbol, value);
    }

    bool Step(const std::int32_t node)
    {
        if (++result.instructions > limit)
        {
            result.limitReached = true;
            result.diagnostics.push_back("Instruction limit reached; preview stopped before mutating editor data.");
            return false;
        }
        if (evaluationDepth >= 256u)
        {
            result.limitReached = true;
            result.diagnostics.push_back("Preview evaluation depth limit reached.");
            return false;
        }
        ++evaluationDepth;
        return true;
    }

    void Leave(const std::int32_t)
    {
        --evaluationDepth;
    }

    SandboxValue Literal(const std::int32_t node) const
    {
        SandboxValue value;
        if (!ValidNode(document, node))
            return value;
        const auto &item = document.SpliceNodes()[static_cast<std::size_t>(node)];
        if (item.tag == kS32)
        {
            value.kind = SandboxValue::Kind::Int;
            value.integer = item.intValue;
        }
        else if (item.tag == kF32)
        {
            value.kind = SandboxValue::Kind::Float;
            value.number = item.floatValue[0];
        }
        else if (item.tag == kVector || item.tag == kClq || item.tag == kSmp)
        {
            value.kind = item.tag == kVector ? SandboxValue::Kind::Vector
                         : item.tag == kClq  ? SandboxValue::Kind::Clq
                                             : SandboxValue::Kind::Smp;
            std::copy_n(item.floatValue, 3, value.components);
        }
        else if (item.tag == kMatrix)
        {
            value.kind = SandboxValue::Kind::Matrix;
            std::copy_n(item.floatValue, 16, value.components);
        }
        else if (item.tag == kLm)
        {
            value.kind = SandboxValue::Kind::Lm;
            std::copy_n(item.floatValue, 2, value.components);
        }
        else if (item.tag == kBool)
        {
            value.kind = SandboxValue::Kind::Bool;
            value.integer = item.intValue != 0;
        }
        else if (item.tag == kSymid)
        {
            value.kind = SandboxValue::Kind::Symbol;
            value.integer = item.intValue;
        }
        else if (item.tag == kBasic)
        {
            value.kind = SandboxValue::Kind::Object;
            value.integer = item.intValue == 0 ? -1 : item.intValue;
        }
        else if (item.tag == kBifk)
        {
            value.kind = SandboxValue::Kind::Builtin;
            value.integer = item.intValue;
        }
        else if (item.tag == 0)
            value.kind = SandboxValue::Kind::None;
        else if (item.tag == kProc)
            value.kind = SandboxValue::Kind::Procedure;
        else if (item.tag == kMethod)
            value.kind = SandboxValue::Kind::Method;
        else if (item.tag == kVoid)
            value.kind = SandboxValue::Kind::Void;
        else if (item.tag == kErr)
            value.kind = SandboxValue::Kind::Error;
        else
        {
            value.kind = SandboxValue::Kind::Quoted;
            value.node = node;
        }
        return value;
    }

    SandboxValue Eval(std::int32_t node)
    {
        SandboxValue value;
        if (!ValidNode(document, node) || !Running())
        {
            value.kind = SandboxValue::Kind::Nil;
            return value;
        }
        if (!Step(node))
            return value;
        const auto &item = document.SpliceNodes()[static_cast<std::size_t>(node)];
        switch (item.tag)
        {
        case 0:
            value.kind = SandboxValue::Kind::None;
            break;
        case kS32:
            value.kind = SandboxValue::Kind::Int;
            value.integer = item.intValue;
            break;
        case kF32:
            value.kind = SandboxValue::Kind::Float;
            value.number = item.floatValue[0];
            break;
        case kVector:
        case kMatrix:
        case kClq:
        case kLm:
        case kSmp:
            value = Literal(node);
            break;
        case kBool:
            value.kind = SandboxValue::Kind::Bool;
            value.integer = item.intValue != 0;
            break;
        case kBasic:
            value.kind = SandboxValue::Kind::Object;
            value.integer = item.intValue == 0 ? -1 : item.intValue;
            break;
        case kBifk:
            value.kind = SandboxValue::Kind::Builtin;
            value.integer = item.intValue;
            break;
        case kProc:
            value.kind = SandboxValue::Kind::Procedure;
            value.node = node;
            break;
        case kMethod:
            value.kind = SandboxValue::Kind::Method;
            value.node = node;
            break;
        case kVoid:
            value.kind = SandboxValue::Kind::Void;
            break;
        case kErr:
            value.kind = SandboxValue::Kind::Error;
            break;
        case kSymid:
            if (const auto found = bindings.find(static_cast<std::uint32_t>(item.intValue)); found != bindings.end())
                value = found->second;
            else
            {
                value.kind = SandboxValue::Kind::Error;
                result.diagnostics.push_back("Unbound symbol " + std::to_string(item.intValue) + ".");
            }
            break;
        case kPair:
            value = EvalList(item.childNode);
            break;
        default:
            value.kind = SandboxValue::Kind::Void;
            break;
        }
        Leave(node);
        return value;
    }

    SandboxValue EvalList(const std::int32_t head)
    {
        SandboxValue value;
        if (!ValidNode(document, head))
        {
            value.kind = SandboxValue::Kind::Nil;
            return value;
        }
        const auto &op = document.SpliceNodes()[static_cast<std::size_t>(head)];
        if (op.tag == kDefine || op.tag == kSet)
        {
            const std::int32_t symbol = op.nextNode;
            if (!ValidNode(document, symbol))
                return value;
            const auto &symbolNode = document.SpliceNodes()[static_cast<std::size_t>(symbol)];
            const SandboxValue assigned = Eval(symbolNode.nextNode);
            const auto symbolId = static_cast<std::uint32_t>(symbolNode.intValue);
            // RefSetBinding changes an existing binding; it never defines one.
            if (op.tag == kDefine || bindings.contains(symbolId))
                bindings[symbolId] = assigned;
            return assigned;
        }
        if (op.tag == kIf)
        {
            const std::int32_t condition = op.nextNode;
            const std::int32_t yes = ValidNode(document, condition) ? document.SpliceNodes()[condition].nextNode : -1;
            const std::int32_t no = ValidNode(document, yes) ? document.SpliceNodes()[yes].nextNode : -1;
            const auto test = Eval(condition);
            if (test.kind != SandboxValue::Kind::Bool)
                Approximation("Non-Boolean if conditions use the runtime's Boolean payload; preview assumes a fresh zero payload.");
            // RefEvalIf tests m_bool == 1, unlike and/or/cond/while truthiness.
            if (test.kind == SandboxValue::Kind::Bool && test.integer == 1)
                return Eval(yes);
            if (ValidNode(document, no))
                return Eval(no);
            value.kind = SandboxValue::Kind::Void;
            return value;
        }
        if (op.tag == kAssert)
        {
            const std::int32_t expression = op.nextNode;
            if (ValidNode(document, expression))
            {
                value = Eval(expression);
                const bool asserted = value.kind == SandboxValue::Kind::Bool    ? value.integer != 0
                                      : value.kind == SandboxValue::Kind::Int   ? value.integer != 0
                                      : value.kind == SandboxValue::Kind::Float ? value.number != 0.0f
                                                                                : true;
                if (!asserted)
                    result.diagnostics.push_back("Splice assertion evaluated false in the safe preview.");
            }
            value = {};
            value.kind = SandboxValue::Kind::Void;
            return value;
        }
        if (op.tag == kOr || op.tag == kAnd)
        {
            value.kind = SandboxValue::Kind::Bool;
            value.integer = op.tag == kAnd;
            for (std::int32_t arg = op.nextNode; Running() && ValidNode(document, arg);
                 arg = document.SpliceNodes()[arg].nextNode)
            {
                value = Eval(arg);
                if ((op.tag == kOr && value.Truthy()) || (op.tag == kAnd && !value.Truthy()))
                    return value;
            }
            return value;
        }
        if (op.tag == kBegin)
        {
            value.kind = SandboxValue::Kind::Void;
            for (std::int32_t expr = op.nextNode; Running() && ValidNode(document, expr);
                 expr = document.SpliceNodes()[expr].nextNode)
                value = Eval(expr);
            return value;
        }
        if (op.tag == kCond)
        {
            value.kind = SandboxValue::Kind::Void;
            SandboxValue lastTest;
            lastTest.kind = SandboxValue::Kind::Nil;
            for (std::int32_t clauseNode = op.nextNode; Running() && ValidNode(document, clauseNode);
                 clauseNode = document.SpliceNodes()[clauseNode].nextNode)
            {
                const auto &wrapper = document.SpliceNodes()[clauseNode];
                const std::int32_t test = wrapper.tag == kPair ? wrapper.childNode : -1;
                if (!ValidNode(document, test))
                    continue;
                const bool isElse = document.SpliceNodes()[test].tag == kElse;
                if (!isElse)
                    lastTest = Eval(test);
                const bool passed = isElse || lastTest.Truthy();
                if (!passed)
                    continue;
                bool evaluatedBody = false;
                for (std::int32_t body = document.SpliceNodes()[test].nextNode; Running() && ValidNode(document, body);
                     body = document.SpliceNodes()[body].nextNode)
                {
                    value = Eval(body);
                    evaluatedBody = true;
                }
                if (!evaluatedBody)
                    value = lastTest;
                return value;
            }
            return value;
        }
        if (op.tag == kCase)
        {
            value.kind = SandboxValue::Kind::Void;
            const std::int32_t expression = op.nextNode;
            const SandboxValue key = Eval(expression);
            for (std::int32_t clauseNode = ValidNode(document, expression) ? document.SpliceNodes()[expression].nextNode
                                                                           : -1;
                 Running() && ValidNode(document, clauseNode); clauseNode = document.SpliceNodes()[clauseNode].nextNode)
            {
                const auto &wrapper = document.SpliceNodes()[clauseNode];
                const std::int32_t match = wrapper.tag == kPair ? wrapper.childNode : -1;
                if (!ValidNode(document, match))
                    continue;
                bool matched = document.SpliceNodes()[match].tag == kElse;
                if (!matched && document.SpliceNodes()[match].tag == kPair)
                    for (std::int32_t candidate = document.SpliceNodes()[match].childNode;
                         Running() && ValidNode(document, candidate); candidate = document.SpliceNodes()[candidate].nextNode)
                    {
                        const auto possible = Literal(candidate);
                        if (Eqv(possible, key))
                        {
                            matched = true;
                            break;
                        }
                    }
                if (!matched)
                    continue;
                for (std::int32_t body = document.SpliceNodes()[match].nextNode; Running() && ValidNode(document, body);
                     body = document.SpliceNodes()[body].nextNode)
                    value = Eval(body);
                return value;
            }
            return value;
        }
        if (op.tag == kLet)
        {
            Approximation("let uses a simplified frame in this preview; local definitions and captured bindings may differ.");
            value.kind = SandboxValue::Kind::Nil;
            const std::int32_t bindingsNode = op.nextNode;
            std::vector<std::pair<std::uint32_t, SandboxValue>> values;
            for (std::int32_t bindingWrapper =
                     ValidNode(document, bindingsNode) && document.SpliceNodes()[bindingsNode].tag == kPair
                         ? document.SpliceNodes()[bindingsNode].childNode
                         : -1;
                 Running() && ValidNode(document, bindingWrapper);
                 bindingWrapper = document.SpliceNodes()[bindingWrapper].nextNode)
            {
                const std::int32_t symbol = document.SpliceNodes()[bindingWrapper].tag == kPair
                                                ? document.SpliceNodes()[bindingWrapper].childNode
                                                : bindingWrapper;
                if (ValidNode(document, symbol) && document.SpliceNodes()[symbol].tag == kSymid)
                    values.emplace_back(static_cast<std::uint32_t>(document.SpliceNodes()[symbol].intValue),
                                        Eval(document.SpliceNodes()[symbol].nextNode));
            }
            std::vector<ShadowedBinding> shadowed;
            shadowed.reserve(values.size());
            for (const auto &[symbol, assigned] : values)
            {
                shadowed.push_back(Shadow(symbol));
                bindings[symbol] = assigned;
            }
            for (std::int32_t body = ValidNode(document, bindingsNode) ? document.SpliceNodes()[bindingsNode].nextNode
                                                                       : -1;
                 Running() && ValidNode(document, body); body = document.SpliceNodes()[body].nextNode)
                value = Eval(body);
            Restore(shadowed);
            return value;
        }
        if (op.tag == kWhile)
        {
            const std::int32_t condition = op.nextNode;
            if (!ValidNode(document, condition))
            {
                value.kind = SandboxValue::Kind::Void;
                return value;
            }
            const std::int32_t body = ValidNode(document, condition) ? document.SpliceNodes()[condition].nextNode : -1;
            while (Running() && Eval(condition).Truthy())
                if (Running() && ValidNode(document, body))
                    Eval(body); // RefEvalWhile executes one body expression; use begin for a sequence.
            value.kind = SandboxValue::Kind::Void;
            return value;
        }
        if (op.tag == kLambda)
        {
            value.kind = SandboxValue::Kind::Lambda;
            value.node = head;
            return value;
        }
        if (op.tag == kQuote)
        {
            return Literal(op.nextNode);
        }
        if (op.tag == kImport)
        {
            const std::int32_t importedNode = op.nextNode;
            if (ValidNode(document, importedNode) && document.SpliceNodes()[importedNode].tag == kS32)
            {
                const auto imported = document.SpliceNodes()[importedNode].intValue;
                result.trace.push_back("Import splice module " + std::to_string(imported) + ".");
                ImportModule(imported);
            }
            value.kind = SandboxValue::Kind::Void;
            return value;
        }

        SandboxValue callable = Eval(head);
        if (callable.kind == SandboxValue::Kind::Builtin)
            return EvalBuiltin(callable.integer, op.nextNode);
        if (callable.kind == SandboxValue::Kind::Lambda)
            return CallLambda(callable.node, op.nextNode);
        result.diagnostics.push_back("Expression operator is not sandbox-callable.");
        return value;
    }

    SandboxValue CallLambda(const std::int32_t lambdaHead, std::int32_t arguments, const bool eventDispatch = false)
    {
        SandboxValue value;
        value.kind = SandboxValue::Kind::Void;
        if (!ValidNode(document, lambdaHead))
            return value;
        if (lambdaCallDepth != 0)
            Approximation("Nested procedure calls use a simplified lexical environment in this preview.");
        ++lambdaCallDepth;
        const auto &lambda = document.SpliceNodes()[lambdaHead];
        const std::int32_t formalsWrapper = lambda.nextNode;
        const std::int32_t body =
            ValidNode(document, formalsWrapper) ? document.SpliceNodes()[formalsWrapper].nextNode : -1;
        std::int32_t formal = ValidNode(document, formalsWrapper) && document.SpliceNodes()[formalsWrapper].tag == kPair
                                  ? document.SpliceNodes()[formalsWrapper].childNode
                                  : formalsWrapper;
        std::size_t requiredArguments = 0;
        bool variadic = false;
        for (auto parameter = formal; ValidNode(document, parameter);
             parameter = document.SpliceNodes()[parameter].nextNode)
        {
            if (document.SpliceNodes()[parameter].tag == 19u)
            {
                variadic = true;
                break;
            }
            ++requiredArguments;
        }
        if (variadic)
            Approximation("Procedure rest-list binding is not modeled exactly by this preview.");
        if (!eventDispatch && requiredArguments + (variadic ? 1u : 0u) > 32u)
        {
            result.diagnostics.push_back("Procedure call exceeds ProjectCane's 32 operand slots.");
            value.kind = SandboxValue::Kind::Error;
            --lambdaCallDepth;
            return value;
        }
        std::vector<SandboxValue> argumentValues;
        for (std::int32_t argument = arguments; Running() && ValidNode(document, argument) &&
             (variadic || argumentValues.size() < requiredArguments);
             argument = document.SpliceNodes()[argument].nextNode)
            argumentValues.push_back(Eval(argument));
        if (!eventDispatch && argumentValues.size() < requiredArguments)
        {
            result.diagnostics.push_back("Procedure call has fewer than its required operands; its body was not executed.");
            value.kind = SandboxValue::Kind::Error;
            --lambdaCallDepth;
            return value;
        }
        std::vector<ShadowedBinding> shadowed;
        std::size_t argumentIndex = 0;
        while (Running() && ValidNode(document, formal) && argumentIndex < argumentValues.size())
        {
            const auto &formalNode = document.SpliceNodes()[formal];
            if (formalNode.tag == kSymid)
            {
                const auto symbol = static_cast<std::uint32_t>(formalNode.intValue);
                shadowed.push_back(Shadow(symbol));
                bindings[symbol] = argumentValues[argumentIndex];
            }
            formal = formalNode.nextNode;
            ++argumentIndex;
        }
        const auto callerParentCount = importedParentCount;
        // A ProjectCane procedure call frame already uses one parent slot for
        // the lambda's captured frame, leaving three slots for dynamic imports.
        importedParentCount = 1;
        for (std::int32_t expr = body; Running() && ValidNode(document, expr);
             expr = document.SpliceNodes()[expr].nextNode)
            value = Eval(expr);
        importedParentCount = callerParentCount;
        Restore(shadowed);
        --lambdaCallDepth;
        return value;
    }

    SandboxValue EvalBuiltin(const std::int32_t id, std::int32_t argument)
    {
        std::vector<SandboxValue> args;
        const bool knownBuiltin = id >= 0 && id < 125;
        const bool variadic = knownBuiltin && kBuiltinVariadic[id] != 0;
        const auto requiredArguments = knownBuiltin ? static_cast<std::size_t>(kBuiltinRequired[id]) : 0u;
        // RefEvalApply ignores surplus operands for fixed-arity built-ins.
        while (Running() && ValidNode(document, argument) &&
               (!knownBuiltin || variadic || args.size() < requiredArguments))
        {
            args.push_back(Eval(argument));
            argument = document.SpliceNodes()[argument].nextNode;
        }
        if (!Running())
            return {};
        if (knownBuiltin && args.size() < requiredArguments)
        {
            result.diagnostics.push_back("Built-in call has fewer than its required operands.");
            SandboxValue error;
            error.kind = SandboxValue::Kind::Error;
            return error;
        }
        const auto numeric = [](const SandboxValue &v)
        {
            if (v.kind == SandboxValue::Kind::Float)
                return v.number;
            if (v.kind == SandboxValue::Kind::Int)
                return static_cast<float>(v.integer);
            return v.kind == SandboxValue::Kind::Bool && v.integer != 0 ? 1.0f : 0.0f;
        };
        const auto integer = [&](const SandboxValue &v) -> std::int32_t
        {
            if (v.kind == SandboxValue::Kind::Int)
                return v.integer;
            if (v.kind == SandboxValue::Kind::Bool)
                return v.integer != 0 ? 1 : 0;
            if (v.kind != SandboxValue::Kind::Float)
                return 0;
            if (std::isfinite(v.number) && static_cast<double>(v.number) >= INT32_MIN &&
                static_cast<double>(v.number) <= INT32_MAX)
                return static_cast<std::int32_t>(v.number);
            Approximation("Out-of-range float-to-integer coercion is not modeled as a runtime result in this preview.");
            return 0;
        };
        SandboxValue out;
        if (id >= 0 && id <= 3 && !args.empty() && args.front().kind == SandboxValue::Kind::Vector)
        {
            out = args.front();
            for (std::size_t index = 1; index < args.size(); ++index)
            {
                if (args[index].kind == SandboxValue::Kind::Vector && id != 3)
                    for (int component = 0; component < 3; ++component)
                        out.components[component] =
                            id == 0   ? out.components[component] + args[index].components[component]
                            : id == 1 ? out.components[component] - args[index].components[component]
                            : out.components[component] * args[index].components[component];
                else if ((id == 2 || id == 3) &&
                         (args[index].kind == SandboxValue::Kind::Int || args[index].kind == SandboxValue::Kind::Float))
                {
                    if (id == 3 && numeric(args[index]) == 0.0f)
                        Approximation("Division by zero is not modeled as a runtime numeric result in this preview.");
                    for (int component = 0; component < 3; ++component)
                        out.components[component] = id == 2 ? out.components[component] * numeric(args[index])
                                                    : numeric(args[index]) != 0.0f
                                                        ? out.components[component] / numeric(args[index])
                                                        : out.components[component];
                }
            }
            return out;
        }
        if (id >= 0 && id <= 3 && !args.empty())
        {
            const bool scalarOperands = std::none_of(args.begin(), args.end(), [](const SandboxValue &v)
                { return v.kind == SandboxValue::Kind::Vector || v.kind == SandboxValue::Kind::Matrix ||
                         v.kind == SandboxValue::Kind::Clq || v.kind == SandboxValue::Kind::Lm; });
            if (!scalarOperands)
                Approximation("Mixed/compound arithmetic is not modeled exactly by this preview.");
            const bool forceFloat = args.front().kind == SandboxValue::Kind::Float ||
                (id != 3 && std::any_of(args.begin(), args.end(), [](const SandboxValue &v)
                    { return v.kind == SandboxValue::Kind::Float; }));
            float total = scalarOperands && id == 0 ? 0.0f : scalarOperands && id == 2 ? 1.0f : numeric(args.front());
            const std::size_t firstOperand = scalarOperands && (id == 0 || id == 2) ? 0u : 1u;
            for (std::size_t i = firstOperand; i < args.size(); ++i)
            {
                if (scalarOperands && args[i].kind != SandboxValue::Kind::Int && args[i].kind != SandboxValue::Kind::Float)
                    continue; // RefOpAdd/Sub/Mult/Div skip nonnumeric subsequent operands.
                if (id == 3 && numeric(args[i]) == 0.0f)
                    Approximation("Division by zero is not modeled as a runtime numeric result in this preview.");
                total = id == 0   ? total + numeric(args[i])
                        : id == 1 ? total - numeric(args[i])
                        : id == 2 ? total * numeric(args[i])
                                  : (numeric(args[i]) != 0.0f ? total / numeric(args[i]) : total);
            }
            out.kind = SandboxValue::Kind::Float;
            out.number = total;
            if (scalarOperands && !forceFloat)
            {
                if (std::isfinite(total) && static_cast<double>(total) >= INT32_MIN &&
                    static_cast<double>(total) <= INT32_MAX)
                {
                    out.kind = SandboxValue::Kind::Int;
                    out.integer = static_cast<std::int32_t>(total);
                }
                else
                    Approximation("Integer arithmetic overflow is not modeled as a runtime result in this preview.");
            }
            return out;
        }
        if (id >= 7 && id <= 11 && args.size() >= 2)
        {
            out.kind = SandboxValue::Kind::Bool;
            out.integer = 1;
            for (std::size_t index = 1; index < args.size(); ++index)
            {
                const float a = numeric(args[index - 1]), b = numeric(args[index]);
                const bool passed = id == 7    ? integer(args[index - 1]) == integer(args[index])
                                    : id == 8  ? a < b
                                    : id == 9  ? a <= b
                                    : id == 10 ? a > b
                                               : a >= b;
                if (!passed)
                {
                    out.integer = 0;
                    break;
                }
            }
            return out;
        }
        if ((id == 12 || id == 13) && args.size() >= 2)
        {
            const auto &a = args[0];
            const auto &b = args[1];
            const bool equal = Eqv(a, b);
            if (id == 13 && !equal && a.kind == SandboxValue::Kind::Quoted &&
                b.kind == SandboxValue::Kind::Quoted && ValidNode(document, a.node) && ValidNode(document, b.node) &&
                document.SpliceNodes()[static_cast<std::size_t>(a.node)].tag == kPair &&
                document.SpliceNodes()[static_cast<std::size_t>(b.node)].tag == kPair)
                Approximation("Deep quoted-list equality is not modeled exactly by this preview.");
            out.kind = SandboxValue::Kind::Bool;
            out.integer = equal;
            return out;
        }
        if (id >= 14 && id <= 29 && !args.empty())
        {
            const auto kind = args[0].kind;
            bool matches = false;
            switch (id)
            {
            case 14:
                matches = kind == SandboxValue::Kind::Bool;
                break;
            case 15:
                matches = kind == SandboxValue::Kind::Int || kind == SandboxValue::Kind::Float;
                break;
            case 16:
                matches = kind == SandboxValue::Kind::Int;
                break;
            case 17:
                matches = kind == SandboxValue::Kind::Float;
                break;
            case 18:
                matches = kind == SandboxValue::Kind::Symbol;
                break;
            case 19:
                matches = kind == SandboxValue::Kind::Vector;
                break;
            case 20:
                matches = kind == SandboxValue::Kind::Matrix;
                break;
            case 21:
                matches = kind == SandboxValue::Kind::Clq;
                break;
            case 22:
                matches = kind == SandboxValue::Kind::Lm;
                break;
            case 23:
                matches = kind == SandboxValue::Kind::Smp;
                break;
            case 24:
                matches = kind == SandboxValue::Kind::None || kind == SandboxValue::Kind::Nil ||
                          (kind == SandboxValue::Kind::Quoted && ValidNode(document, args[0].node) &&
                           document.SpliceNodes()[static_cast<std::size_t>(args[0].node)].tag == kPair);
                break;
            case 25:
                matches = kind == SandboxValue::Kind::None || kind == SandboxValue::Kind::Nil ||
                          (kind == SandboxValue::Kind::Quoted && ValidNode(document, args[0].node) &&
                           document.SpliceNodes()[static_cast<std::size_t>(args[0].node)].tag == kPair &&
                           document.SpliceNodes()[static_cast<std::size_t>(args[0].node)].childNode == -1);
                break;
            case 26:
                matches = kind == SandboxValue::Kind::Object;
                break;
            case 27:
                matches = kind == SandboxValue::Kind::Object && args[0].integer < 0;
                break;
            case 28:
                matches = kind == SandboxValue::Kind::Method;
                break;
            case 29:
                matches = kind == SandboxValue::Kind::Lambda || kind == SandboxValue::Kind::Builtin ||
                          kind == SandboxValue::Kind::Procedure || kind == SandboxValue::Kind::Method;
                break;
            default:
                matches = false;
                break;
            }
            out.kind = SandboxValue::Kind::Bool;
            out.integer = matches;
            return out;
        }
        if (id == 31 && !args.empty())
        {
            out.kind = SandboxValue::Kind::Bool;
            out.integer = !args[0].Truthy();
            return out;
        }
        if (id == 46)
        {
            out.kind = SandboxValue::Kind::Vector;
            for (std::size_t component = 0; component < 3 && component < args.size(); ++component)
                out.components[component] = numeric(args[component]);
            return out;
        }
        if (id >= 54 && id <= 62 && !args.empty())
        {
            const float first = numeric(args[0]);
            out.kind = SandboxValue::Kind::Float;
            switch (id)
            {
            case 54:
                out.number = std::sqrt(first);
                break;
            case 55:
                out.number = std::sin(first);
                break;
            case 56:
                out.number = std::cos(first);
                break;
            case 57:
                out.number = std::tan(first);
                break;
            case 58:
                out.number = std::asin(first);
                break;
            case 59:
                out.number = std::acos(first);
                break;
            case 60:
                out.number = std::atan(first);
                break;
            case 61:
                out.number = first;
                while (Running() && out.number > 3.1415926535897932385f)
                {
                    if (++result.instructions > limit)
                    {
                        result.limitReached = true;
                        result.diagnostics.push_back("Angle normalization reached the preview instruction limit.");
                        break;
                    }
                    out.number -= 6.2831853071795864769f;
                }
                while (Running() && out.number < -3.1415926535897932385f)
                {
                    if (++result.instructions > limit)
                    {
                        result.limitReached = true;
                        result.diagnostics.push_back("Angle normalization reached the preview instruction limit.");
                        break;
                    }
                    out.number += 6.2831853071795864769f;
                }
                break;
            case 62:
                out.number = args.size() >= 2 ? std::atan2(first, numeric(args[1])) : 0.0f;
                break;
            }
            return out;
        }
        if (id >= 63 && id <= 68 && !args.empty())
        {
            const auto asVector = [](const SandboxValue &v)
            {
                if (v.kind == SandboxValue::Kind::Vector)
                    return v;
                SandboxValue zero;
                zero.kind = SandboxValue::Kind::Vector;
                return zero;
            };
            const auto firstVector = asVector(args[0]);
            const auto secondVector = args.size() > 1 ? asVector(args[1]) : asVector(SandboxValue{});
            const auto lengthSquared = [](const SandboxValue &value)
            {
                return value.components[0] * value.components[0] + value.components[1] * value.components[1] +
                       value.components[2] * value.components[2];
            };
            if (id == 68)
            {
                out = firstVector;
                const float length = std::sqrt(lengthSquared(out));
                if (length >= 0.0001f)
                    for (float &component : out.components)
                        component /= length;
                else
                    out = secondVector;
                return out;
            }
            if (id == 65)
            {
                out.kind = SandboxValue::Kind::Float;
                out.number = std::sqrt(lengthSquared(firstVector));
                return out;
            }
            if (id == 64)
            {
                out.kind = SandboxValue::Kind::Vector;
                out.components[0] =
                    firstVector.components[1] * secondVector.components[2] - firstVector.components[2] * secondVector.components[1];
                out.components[1] =
                    firstVector.components[2] * secondVector.components[0] - firstVector.components[0] * secondVector.components[2];
                out.components[2] =
                    firstVector.components[0] * secondVector.components[1] - firstVector.components[1] * secondVector.components[0];
                return out;
            }
            out.kind = SandboxValue::Kind::Float;
            if (id == 63)
                out.number = firstVector.components[0] * secondVector.components[0] +
                             firstVector.components[1] * secondVector.components[1] +
                             firstVector.components[2] * secondVector.components[2];
            else
            {
                SandboxValue delta = firstVector;
                for (int component = 0; component < 3; ++component)
                    delta.components[component] -= secondVector.components[component];
                const float squared = lengthSquared(delta);
                out.number = id == 67 ? squared : std::sqrt(squared);
            }
            return out;
        }
        if (id >= 88 && id <= 95 && !args.empty())
        {
            out.kind = SandboxValue::Kind::Float;
            const float first = numeric(args[0]);
            switch (id)
            {
            case 88:
                out.number = std::floor(first);
                break;
            case 89:
                out.number = std::ceil(first);
                break;
            case 90:
                out.number = std::floor(first + 0.5f);
                break;
            case 91:
                out.number = std::trunc(first);
                break;
            case 92:
                if (args[0].kind == SandboxValue::Kind::Int)
                {
                    out.kind = SandboxValue::Kind::Int;
                    if (args[0].integer == INT32_MIN)
                    {
                        Approximation("Integer abs overflow is not modeled as a runtime result in this preview.");
                        out.integer = 0;
                    }
                    else
                        out.integer = args[0].integer < 0 ? -args[0].integer : args[0].integer;
                }
                else
                    out.number = std::fabs(first);
                break;
            case 93:
                out = args.front();
                for (std::size_t i = 1; i < args.size(); ++i)
                    if (numeric(args[i]) > numeric(out))
                        out = args[i];
                break;
            case 94:
                out = args.front();
                for (std::size_t i = 1; i < args.size(); ++i)
                    if (numeric(args[i]) < numeric(out))
                        out = args[i];
                break;
            case 95:
            {
                const auto lhs = integer(args[0]);
                const auto rhs = integer(args[1]);
                out.kind = SandboxValue::Kind::Int;
                if (lhs == INT32_MIN && rhs == -1)
                {
                    Approximation("Integer modulo overflow is not modeled as a runtime result in this preview.");
                    out.integer = 0;
                }
                else
                    out.integer = rhs != 0 ? lhs % rhs : 0;
                break;
            }
            }
            if (id <= 91)
            {
                SandboxValue numericResult;
                numericResult.kind = SandboxValue::Kind::Float;
                numericResult.number = out.number;
                out.kind = SandboxValue::Kind::Int;
                out.integer = integer(numericResult);
            }
            return out;
        }
        if (id >= 97)
        {
            result.approximated = true;
            std::string action = SpliceBuiltinName(id);
            action += "(";
            for (std::size_t index = 0; index < args.size(); ++index)
            {
                if (index != 0)
                    action += ", ";
                const auto &argumentValue = args[index];
                switch (argumentValue.kind)
                {
                case SandboxValue::Kind::Int:
                    action += std::to_string(argumentValue.integer);
                    break;
                case SandboxValue::Kind::Float:
                    action += std::to_string(argumentValue.number);
                    break;
                case SandboxValue::Kind::Bool:
                    action += argumentValue.integer != 0 ? "#t" : "#f";
                    break;
                case SandboxValue::Kind::Symbol:
                    action += "symbol:" + std::to_string(argumentValue.integer);
                    break;
                case SandboxValue::Kind::Object:
                    action += "object-token:" + std::to_string(argumentValue.integer);
                    break;
                case SandboxValue::Kind::Lambda:
                    action += "lambda";
                    break;
                case SandboxValue::Kind::Procedure:
                    action += "procedure";
                    break;
                case SandboxValue::Kind::Builtin:
                    action += std::string("builtin:") + SpliceBuiltinName(argumentValue.integer);
                    break;
                case SandboxValue::Kind::Method:
                    action += "method";
                    break;
                case SandboxValue::Kind::Vector:
                    action += "vector";
                    break;
                case SandboxValue::Kind::Matrix:
                    action += "matrix";
                    break;
                case SandboxValue::Kind::Clq:
                    action += "clq";
                    break;
                case SandboxValue::Kind::Lm:
                    action += "lm";
                    break;
                case SandboxValue::Kind::Smp:
                    action += "smp";
                    break;
                case SandboxValue::Kind::Error:
                    action += "error";
                    break;
                case SandboxValue::Kind::Quoted:
                    action += "quoted-node:" + std::to_string(argumentValue.node);
                    break;
                default:
                    action += "void";
                    break;
                }
            }
            action += ") (sandboxed; document unchanged)";
            result.nativeActions.push_back(std::move(action));
            return out;
        }
        result.approximated = true;
        result.diagnostics.push_back(
            std::string(SpliceBuiltinName(id)) +
            " is not modeled exactly by the safe interpreter; its return value is a placeholder.");
        result.trace.push_back(std::string(SpliceBuiltinName(id)) + " evaluated as a safe placeholder.");
        return out;
    }
};
}

const char *SpliceEventName(const std::uint32_t eventId)
{
    static constexpr const char *names[] = {"Post-load",
                                            "Update",
                                            "Trigger",
                                            "Untrigger",
                                            "Added",
                                            "Removed",
                                            "Touch",
                                            "Untouch",
                                            "Death",
                                            "Apply",
                                            "Animation frame",
                                            "Retract",
                                            "Contents changed",
                                            "Fire",
                                            "Enter state",
                                            "Enter StepGuard state",
                                            "Dialog label",
                                            "Apply state machine",
                                            "Retract state machine",
                                            "Basket damage",
                                            "Break damage",
                                            "Retract dialog",
                                            "Restore",
                                            "Script apply",
                                            "Script label",
                                            "Script retract",
                                            "Rhythm start",
                                            "Rhythm event",
                                            "Rhythm stop",
                                            "Barrier impact"};
    return eventId < std::size(names) ? names[eventId] : "Class-specific event";
}

const char *SpliceBuiltinName(const std::int32_t builtinId)
{
    // Exact g_mpbifkbif order from ProjectCane's eval.cpp.
    static constexpr const char *names[] = {"+",
                                            "-",
                                            "*",
                                            "/",
                                            "print",
                                            "print-frame",
                                            "print-sidebag",
                                            "=",
                                            "<",
                                            "<=",
                                            ">",
                                            ">=",
                                            "eqv?",
                                            "equal?",
                                            "boolean?",
                                            "number?",
                                            "integer?",
                                            "float?",
                                            "symbol?",
                                            "vector?",
                                            "matrix?",
                                            "clq?",
                                            "lm?",
                                            "smp?",
                                            "list?",
                                            "null?",
                                            "object?",
                                            "null-obj?",
                                            "method?",
                                            "procedure?",
                                            "near?",
                                            "not",
                                            "cons",
                                            "car",
                                            "cdr",
                                            "set-car!",
                                            "set-cdr!",
                                            "length",
                                            "nth",
                                            "member?",
                                            "list",
                                            "append",
                                            "map",
                                            "filter",
                                            "for-each",
                                            "eval",
                                            "vector",
                                            "matrix",
                                            "clq",
                                            "lm",
                                            "smp",
                                            "get-element",
                                            "random-seed",
                                            "random",
                                            "sqrt",
                                            "sin",
                                            "cos",
                                            "tan",
                                            "asin",
                                            "acos",
                                            "atan",
                                            "rad-normalize",
                                            "atan2",
                                            "vector-dot-product",
                                            "vector-cross-product",
                                            "vector-lth",
                                            "vector-distance",
                                            "vector-distance-squared",
                                            "vector-normalize",
                                            "vector-project-normal",
                                            "vector-project-tangent",
                                            "vector-ballistic-velocity",
                                            "vector-radian-normal",
                                            "matrix-transpose",
                                            "matrix-invert",
                                            "matrix-calculate-dmat",
                                            "matrix-interpolate-rotate",
                                            "matrix-decompose-to-translate",
                                            "matrix-decompose-to-rotate",
                                            "matrix-decompose-to-euler",
                                            "matrix-decompose-to-radian-normal",
                                            "matrix-look-at",
                                            "matrix-tilt-upright",
                                            "clq-evaluate",
                                            "clq-evaluate-lm",
                                            "clq-fit",
                                            "lm-limit",
                                            "lm-check?",
                                            "floor",
                                            "ceiling",
                                            "round",
                                            "truncate",
                                            "abs",
                                            "max",
                                            "min",
                                            "modulo",
                                            "current-time",
                                            "schedule-callback",
                                            "defer-object-update",
                                            "addo",
                                            "ensureo",
                                            "seto!",
                                            "geto",
                                            "find-object",
                                            "find-objects",
                                            "find-nearest-object",
                                            "find-nearest-objects",
                                            "find-player-object",
                                            "find-world-object",
                                            "find-camera-object",
                                            "find-class-objects",
                                            "find-objects-in-bounding-box",
                                            "find-objects-in-bounding-sphere",
                                            "hit-test-objects",
                                            "hit-test-objects-first",
                                            "convert-object-position",
                                            "convert-object-vector",
                                            "convert-object-matrix",
                                            "near-clip-center",
                                            "start-sound",
                                            "stop-sound",
                                            "start-rumble",
                                            "emit-smoke-cloud",
                                            "predict-animation-effect",
                                            "set-music-register"};
    return builtinId >= 0 && builtinId < static_cast<std::int32_t>(std::size(names)) ? names[builtinId]
                                                                                     : "invalid built-in";
}

std::string FormatSpliceModuleSource(const BrxDocument &document, const std::int32_t spliceIndex)
{
    if (spliceIndex < 0 || static_cast<std::size_t>(spliceIndex) >= document.SpliceModules().size())
        return "; invalid splice module\n";
    std::unordered_set<std::int32_t> active;
    std::function<std::string(std::int32_t, int)> formatExpression;
    const auto formatEditorAlias = [](const std::string &name)
    {
        const bool plain =
            !name.empty() &&
            std::all_of(name.begin(), name.end(), [](const char ch)
                        { return std::isalnum(static_cast<unsigned char>(ch)) || ch == '_' || ch == '-'; });
        if (plain)
            return "@" + name;
        std::string quoted = "@{";
        for (const char ch : name)
        {
            if (ch == '\\' || ch == '}')
                quoted.push_back('\\');
            quoted.push_back(ch);
        }
        quoted.push_back('}');
        return quoted;
    };
    const auto atom = [&](const BrxSpliceNode &node) -> std::string
    {
        const auto formatFloat = [](const float value)
        {
            std::ostringstream out;
            out << std::setprecision(9) << value;
            auto text = out.str();
            if (text.find_first_of(".eE") == std::string::npos)
                text += ".0";
            return text;
        };
        const auto floatPayload = [&](const char *name, const std::size_t count)
        {
            std::ostringstream out;
            out << name << "#[";
            for (std::size_t index = 0; index < count; ++index)
            {
                if (index != 0)
                    out << ',';
                out << formatFloat(node.floatValue[index]);
            }
            out << ']';
            return out.str();
        };
        switch (node.tag)
        {
        case kSet:
            return "set!";
        case kDefine:
            return "define";
        case 3:
            return "define-macro";
        case 4:
            return "define-macro-lambda";
        case kAssert:
            return "assert";
        case kIf:
            return "if";
        case kOr:
            return "or";
        case kAnd:
            return "and";
        case kCond:
            return "cond";
        case kElse:
            return "else";
        case kCase:
            return "case";
        case kLet:
            return "let";
        case kWhile:
            return "while";
        case kLambda:
            return "lambda";
        case kBegin:
            return "begin";
        case kImport:
            return "import";
        case kQuote:
            return "quote";
        case 18:
            return "dot";
        case 19:
            return "pipe";
        case kS32:
            return std::to_string(node.intValue);
        case kF32:
            return formatFloat(node.floatValue[0]);
        case kVector:
            return floatPayload("vector", 3);
        case kMatrix:
            return floatPayload("matrix", 16);
        case kClq:
            return floatPayload("clq", 3);
        case kLm:
            return floatPayload("lm", 2);
        case kSmp:
            return floatPayload("smp", 3);
        case kBool:
            return node.intValue != 0 ? "true" : "false";
        case kSymid:
            return "sym#" + std::to_string(node.intValue);
        case kBifk:
            return SpliceBuiltinName(node.intValue);
        case kBasic:
            return "object#" + std::to_string(node.intValue);
        case kVoid:
            return "void";
        default:
            return "tag#" + std::to_string(node.tag) + ":" + std::to_string(node.intValue);
        }
    };
    formatExpression = [&](const std::int32_t index, const int depth) -> std::string
    {
        if (!ValidNode(document, index))
            return "<invalid-node#" + std::to_string(index) + ">";
        if (depth > 256)
            return "<depth-limit>";
        if (!active.insert(index).second)
            return "<cycle#" + std::to_string(index) + ">";
        const auto &node = document.SpliceNodes()[static_cast<std::size_t>(index)];
        std::string result;
        if (node.tag == kPair)
        {
            // Present an exact native (find-object OID null-basis) as
            // @EditorName only
            // when that OID identifies one decoded object. Duplicate runtime
            // OIDs remain numeric because their lookup is context-sensitive.
            if (ValidNode(document, node.childNode))
            {
                const auto &head = document.SpliceNodes()[static_cast<std::size_t>(node.childNode)];
                if (head.tag == kBifk && head.intValue == 103 && ValidNode(document, head.nextNode))
                {
                    const auto &oid = document.SpliceNodes()[static_cast<std::size_t>(head.nextNode)];
                    const auto basisIndex = oid.nextNode;
                    const bool nullBasis = ValidNode(document, basisIndex) &&
                                           document.SpliceNodes()[static_cast<std::size_t>(basisIndex)].tag == 0 &&
                                           document.SpliceNodes()[static_cast<std::size_t>(basisIndex)].nextNode < 0;
                    if (oid.tag == kS32 && nullBasis)
                    {
                        std::int32_t match = -1;
                        for (std::size_t objectIndex = 0; objectIndex < document.Objects().size(); ++objectIndex)
                        {
                            if (document.Objects()[objectIndex].oid != oid.intValue)
                                continue;
                            if (match >= 0)
                            {
                                match = -2;
                                break;
                            }
                            match = static_cast<std::int32_t>(objectIndex);
                        }
                        if (match >= 0 && !document.Objects()[static_cast<std::size_t>(match)].editorName.empty())
                        {
                            const auto &name = document.Objects()[static_cast<std::size_t>(match)].editorName;
                            if (document.FindObjectByEditorName(name) == match)
                            {
                                active.erase(index);
                                return formatEditorAlias(name);
                            }
                        }
                    }
                }
            }
            result = "(";
            std::unordered_set<std::int32_t> siblings;
            auto item = node.childNode;
            bool first = true;
            std::size_t itemCount = 0;
            while (ValidNode(document, item) && siblings.insert(item).second)
            {
                if (!first)
                    result += "\n" + std::string(static_cast<std::size_t>((depth + 1) * 2), ' ');
                result += formatExpression(item, depth + 1);
                first = false;
                ++itemCount;
                item = document.SpliceNodes()[static_cast<std::size_t>(item)].nextNode;
            }
            if (item >= 0)
                result += " . <invalid-or-cycle#" + std::to_string(item) + ">";
            if (itemCount > 1)
                result += "\n" + std::string(static_cast<std::size_t>(depth * 2), ' ');
            result += ")";
        }
        else
            result = atom(node);
        active.erase(index);
        return result;
    };
    const auto root = document.SpliceModules()[static_cast<std::size_t>(spliceIndex)].rootNode;
    return "; ProjectCane Splice module " + std::to_string(spliceIndex) +
           "\n; sym#N preserves unresolved symbols; payload literals preserve exact numeric data.\n" +
           formatExpression(root, 0) + "\n";
}

namespace
{
struct ParsedSpliceSource
{
    SpliceSourceResult result;
    std::vector<BrxSpliceNode> nodes;
    std::int32_t root = -1;
    std::unordered_set<std::int32_t> cidResolvedPropertyNodes;
    std::vector<std::pair<std::uint32_t, std::uint32_t>> nodeLocations;
};

class SpliceSourceParser
{
  public:
    explicit SpliceSourceParser(const std::string &source, const std::int16_t selectedCid,
                                const std::uint32_t symbolBase = 0, const BrxDocument *editorAliases = nullptr)
        : source_(source), selectedCid_(selectedCid), editorAliases_(editorAliases),
          nextSymbol_((std::max)(symbolBase, 30u))
    {
        Next();
    }

    ParsedSpliceSource Parse()
    {
        ParsedSpliceSource parsed;
        output_ = &parsed;
        if (token_.kind == TokenKind::End)
            Error(token_, "The source contains no expression.");
        else
            parsed.root = ParseExpression();
        if (parsed.root >= 0 && token_.kind != TokenKind::End)
            Error(token_, "Only one module-root expression is allowed.");
        parsed.result.nodeCount = static_cast<std::uint32_t>(parsed.nodes.size());
        parsed.result.requiredSymbolCount = nextSymbol_;
        parsed.result.nodeLocations = parsed.nodeLocations;
        parsed.result.succeeded = parsed.root >= 0 && parsed.result.diagnostics.empty();
        output_ = nullptr;
        return parsed;
    }

  private:
    enum class TokenKind
    {
        Left,
        Right,
        Atom,
        End
    };
    struct Token
    {
        TokenKind kind = TokenKind::End;
        std::string text;
        std::uint32_t line = 1, column = 1;
    };

    void SkipWhitespace()
    {
        for (;;)
        {
            while (position_ < source_.size() && (source_[position_] == ' ' || source_[position_] == '\t' ||
                                                  source_[position_] == '\r' || source_[position_] == '\n'))
                Advance();
            if (position_ >= source_.size() || source_[position_] != ';')
                break;
            while (position_ < source_.size() && source_[position_] != '\n')
                Advance();
        }
    }

    void Advance()
    {
        if (source_[position_++] == '\n')
        {
            ++line_;
            column_ = 1;
        }
        else
            ++column_;
    }

    void Next()
    {
        SkipWhitespace();
        token_ = {};
        token_.line = line_;
        token_.column = column_;
        if (position_ >= source_.size())
        {
            token_.kind = TokenKind::End;
            return;
        }
        if (source_[position_] == '(')
        {
            token_.kind = TokenKind::Left;
            token_.text = "(";
            Advance();
            return;
        }
        if (source_[position_] == ')')
        {
            token_.kind = TokenKind::Right;
            token_.text = ")";
            Advance();
            return;
        }
        token_.kind = TokenKind::Atom;
        // Editor-only aliases may contain spaces and punctuation.  Keep them
        // lossless with @{...}; backslash escapes the following character.
        if (source_[position_] == '@' && position_ + 1 < source_.size() && source_[position_ + 1] == '{')
        {
            token_.text = "@{";
            Advance();
            Advance();
            bool escaped = false;
            while (position_ < source_.size())
            {
                const char ch = source_[position_];
                token_.text.push_back(ch);
                Advance();
                if (escaped)
                {
                    escaped = false;
                    continue;
                }
                if (ch == '\\')
                {
                    escaped = true;
                    continue;
                }
                if (ch == '}')
                    return;
            }
            return;
        }
        while (position_ < source_.size() && source_[position_] != '(' && source_[position_] != ')' &&
               source_[position_] != ';' && source_[position_] != ' ' && source_[position_] != '\t' &&
               source_[position_] != '\r' && source_[position_] != '\n')
        {
            token_.text.push_back(source_[position_]);
            Advance();
        }
    }

    void Error(const Token &token, const std::string &message)
    {
        output_->result.diagnostics.push_back({token.line, token.column, message});
    }

    std::int32_t AddNode(const std::uint8_t tag)
    {
        if (output_->nodes.size() >= 1'000'000)
        {
            Error(token_, "The source exceeds the safe one-million-node authoring limit.");
            return -1;
        }
        BrxSpliceNode node;
        node.tag = tag;
        node.modified = true;
        node.editorCreated = true;
        output_->nodes.push_back(node);
        output_->nodeLocations.emplace_back(expressionOrigin_.line, expressionOrigin_.column);
        return static_cast<std::int32_t>(output_->nodes.size() - 1);
    }

    std::int32_t ParseExpression(const std::uint32_t depth = 0)
    {
        const auto current = token_;
        if (depth > 256u)
        {
            Error(current, "Source nesting exceeds the editor's 256-level source limit.");
            return -1;
        }
        if (current.kind == TokenKind::Right)
        {
            Error(current, "Unexpected closing parenthesis.");
            Next();
            return -1;
        }
        if (current.kind == TokenKind::End)
        {
            Error(current, "Expected an expression before the end of the source.");
            return -1;
        }
        if (current.kind == TokenKind::Left)
        {
            Next();
            expressionOrigin_ = current;
            const auto pair = AddNode(kPair);
            std::int32_t first = -1, previous = -1;
            while (token_.kind != TokenKind::Right && token_.kind != TokenKind::End)
            {
                const auto item = ParseExpression(depth + 1u);
                if (item < 0)
                    break;
                if (first < 0)
                    first = item;
                if (previous >= 0)
                    output_->nodes[static_cast<std::size_t>(previous)].nextNode = item;
                previous = item;
            }
            if (token_.kind != TokenKind::Right)
                Error(current, "Unclosed list; expected a closing parenthesis.");
            else
                Next();
            if (pair >= 0)
                output_->nodes[static_cast<std::size_t>(pair)].childNode = first;
            return pair;
        }
        Next();
        expressionOrigin_ = current;
        return ParseAtom(current);
    }

    std::int32_t ParseAtom(const Token &token)
    {
        static constexpr std::pair<const char *, std::uint8_t> keywords[] = {
            {"set!", kSet},      {"define", kDefine}, {"define-macro", 3}, {"define-macro-lambda", 4},
            {"assert", kAssert}, {"if", kIf},         {"or", kOr},         {"and", kAnd},
            {"cond", kCond},     {"else", kElse},     {"case", kCase},     {"let", kLet},
            {"while", kWhile},   {"lambda", kLambda}, {"begin", kBegin},   {"import", kImport},
            {"quote", kQuote},   {"dot", 18},         {"pipe", 19},        {"void", kVoid}};
        for (const auto &[name, tag] : keywords)
            if (token.text == name)
                return AddNode(tag);
        if (token.text == "true" || token.text == "false")
        {
            const auto node = AddNode(kBool);
            if (node >= 0)
                output_->nodes[static_cast<std::size_t>(node)].intValue = token.text == "true" ? 1 : 0;
            return node;
        }
        struct PayloadKind
        {
            const char *prefix;
            std::uint8_t tag;
            std::size_t count;
        };
        static constexpr PayloadKind payloadKinds[] = {{"vector#[", kVector, 3},
                                                       {"matrix#[", kMatrix, 16},
                                                       {"clq#[", kClq, 3},
                                                       {"lm#[", kLm, 2},
                                                       {"smp#[", kSmp, 3}};
        for (const auto &payloadKind : payloadKinds)
        {
            const std::string_view text(token.text);
            const std::string_view prefix(payloadKind.prefix);
            if (!text.starts_with(prefix))
                continue;
            if (text.empty() || text.back() != ']')
            {
                Error(token, "Malformed payload literal; expected a closing ].");
                return -1;
            }
            const auto body = text.substr(prefix.size(), text.size() - prefix.size() - 1);
            std::vector<float> values;
            std::size_t start = 0;
            while (start <= body.size())
            {
                const auto comma = body.find(',', start);
                const auto part =
                    body.substr(start, comma == std::string_view::npos ? body.size() - start : comma - start);
                std::string owned(part);
                char *end = nullptr;
                const auto value = std::strtof(owned.c_str(), &end);
                if (owned.empty() || !end || *end != '\0' || !std::isfinite(value))
                {
                    Error(token, "Payload literals require finite comma-separated floating-point values.");
                    return -1;
                }
                values.push_back(value);
                if (comma == std::string_view::npos)
                    break;
                start = comma + 1;
            }
            if (values.size() != payloadKind.count)
            {
                Error(token, std::string(payloadKind.prefix).substr(0, std::string(payloadKind.prefix).size() - 2) +
                                 " requires exactly " + std::to_string(payloadKind.count) + " components.");
                return -1;
            }
            const auto node = AddNode(payloadKind.tag);
            if (node >= 0)
                std::copy(values.begin(), values.end(), output_->nodes[static_cast<std::size_t>(node)].floatValue);
            return node;
        }
        const auto numericSuffix = [&](const char *prefix, std::int32_t &value)
        {
            const std::string_view text(token.text);
            const std::string_view start(prefix);
            if (!text.starts_with(start))
                return false;
            const auto suffix = text.substr(start.size());
            const auto converted = std::from_chars(suffix.data(), suffix.data() + suffix.size(), value);
            return converted.ec == std::errc{} && converted.ptr == suffix.data() + suffix.size();
        };
        std::int32_t value = 0;
        if (numericSuffix("sym#", value) || numericSuffix("object#", value))
        {
            const bool symbol = token.text.starts_with("sym#");
            if (symbol)
            {
                if (value < 0 || value >= 1'000'000)
                {
                    Error(token, "Symbol IDs must be between 0 and 999999.");
                    return -1;
                }
                nextSymbol_ = (std::max)(nextSymbol_, static_cast<std::uint32_t>(value) + 1u);
            }
            else if (value != 0)
            {
                Error(token, "Nonzero object# values are raw runtime pointers and cannot be exported; use find-object or an editor alias.");
                return -1;
            }
            const auto node = AddNode(symbol ? kSymid : kBasic);
            if (node >= 0)
                output_->nodes[static_cast<std::size_t>(node)].intValue = value;
            return node;
        }
        if (token.text.size() > 1 && token.text.front() == '@')
        {
            if (editorAliases_ == nullptr)
            {
                Error(token, "Editor object names are unavailable in this source context.");
                return -1;
            }
            std::string name;
            if (token.text.starts_with("@{"))
            {
                if (token.text.size() < 3 || token.text.back() != '}')
                {
                    Error(token, "Unclosed editor object alias; expected a closing }.");
                    return -1;
                }
                bool escaped = false;
                for (std::size_t index = 2; index + 1 < token.text.size(); ++index)
                {
                    const char ch = token.text[index];
                    if (escaped)
                    {
                        name.push_back(ch);
                        escaped = false;
                    }
                    else if (ch == '\\')
                        escaped = true;
                    else
                        name.push_back(ch);
                }
                if (escaped)
                {
                    Error(token, "Editor object alias ends with an incomplete escape.");
                    return -1;
                }
            }
            else
                name = token.text.substr(1);
            if (name.empty())
            {
                Error(token, "Editor object aliases cannot be empty.");
                return -1;
            }
            const auto objectIndex = editorAliases_->FindObjectByEditorName(name);
            if (objectIndex == -2)
            {
                Error(token, "Editor object name '@" + name + "' is duplicated; rename one of the objects.");
                return -1;
            }
            if (objectIndex < 0 || static_cast<std::size_t>(objectIndex) >= editorAliases_->Objects().size())
            {
                Error(token, "Editor object name '@" + name + "' does not exist in this world.");
                return -1;
            }
            const auto pair = AddNode(kPair);
            const auto builtin = AddNode(kBifk);
            const auto oid = AddNode(kS32);
            const auto basis = AddNode(0);
            if (pair < 0 || builtin < 0 || oid < 0 || basis < 0)
                return -1;
            output_->nodes[static_cast<std::size_t>(pair)].childNode = builtin;
            output_->nodes[static_cast<std::size_t>(builtin)].intValue = 103;
            output_->nodes[static_cast<std::size_t>(builtin)].nextNode = oid;
            output_->nodes[static_cast<std::size_t>(oid)].intValue =
                editorAliases_->Objects()[static_cast<std::size_t>(objectIndex)].oid;
            output_->nodes[static_cast<std::size_t>(oid)].nextNode = basis;
            return pair;
        }
        if (token.text.starts_with("tag#"))
        {
            const auto separator = token.text.find(':');
            std::int32_t tag = -1, payload = 0;
            if (separator != std::string::npos)
            {
                const auto tagText = token.text.substr(4, separator - 4);
                const auto payloadText = token.text.substr(separator + 1);
                const auto tagResult = std::from_chars(tagText.data(), tagText.data() + tagText.size(), tag);
                const auto payloadResult =
                    std::from_chars(payloadText.data(), payloadText.data() + payloadText.size(), payload);
                if (tagResult.ec == std::errc{} && payloadResult.ec == std::errc{} &&
                    (tag == 0 || tag == kProc || tag == kMethod || tag == kErr))
                {
                    const auto node = AddNode(static_cast<std::uint8_t>(tag));
                    if (node >= 0)
                        output_->nodes[static_cast<std::size_t>(node)].intValue = payload;
                    return node;
                }
            }
            Error(
                token,
                "Malformed or unsafe explicit tag. Only tag#0, tag#31, tag#33, and tag#35 are accepted; use dedicated syntax for other tags.");
            return -1;
        }
        std::int32_t integer = 0;
        std::string_view integerText(token.text);
        if (integerText.size() > 1 && integerText.front() == '+' &&
            std::isdigit(static_cast<unsigned char>(integerText[1])))
            integerText.remove_prefix(1);
        const auto parsedInteger = std::from_chars(integerText.data(), integerText.data() + integerText.size(), integer);
        if (!integerText.empty() && parsedInteger.ptr == integerText.data() + integerText.size() &&
            parsedInteger.ec == std::errc::result_out_of_range)
        {
            Error(token, "Integer literals must fit ProjectCane's signed 32-bit payload.");
            return -1;
        }
        if (parsedInteger.ec == std::errc{} && parsedInteger.ptr == integerText.data() + integerText.size())
        {
            const auto node = AddNode(kS32);
            if (node >= 0)
                output_->nodes[static_cast<std::size_t>(node)].intValue = static_cast<std::int32_t>(integer);
            return node;
        }
        char *floatEnd = nullptr;
        const auto floating = std::strtof(token.text.c_str(), &floatEnd);
        if (floatEnd && *floatEnd == '\0' && token.text.find_first_of(".eE") != std::string::npos)
        {
            if (!std::isfinite(floating))
            {
                Error(token, "Floating-point literals must be finite.");
                return -1;
            }
            const auto node = AddNode(kF32);
            if (node >= 0)
                output_->nodes[static_cast<std::size_t>(node)].floatValue[0] = floating;
            return node;
        }
        for (std::int32_t id = 0; id < 512; ++id)
        {
            const auto *name = SpliceBuiltinName(id);
            if (std::string_view(name) == "invalid built-in")
                break;
            if (token.text == name)
            {
                const auto node = AddNode(kBifk);
                if (node >= 0)
                    output_->nodes[static_cast<std::size_t>(node)].intValue = id;
                return node;
            }
        }
        for (std::uint32_t id = 0; id < 30; ++id)
            if (token.text == SpliceEventName(id))
            {
                const auto node = AddNode(kSymid);
                if (node >= 0)
                {
                    output_->nodes[static_cast<std::size_t>(node)].intValue = static_cast<std::int32_t>(id);
                    nextSymbol_ = (std::max)(nextSymbol_, id + 1u);
                }
                return node;
            }
        if (selectedCid_ >= 0 && static_cast<std::size_t>(selectedCid_) < CidOptionIds().size())
        {
            const auto normalize = [](const std::string_view text)
            {
                std::string result;
                for (const auto ch : text)
                    if (std::isalnum(static_cast<unsigned char>(ch)))
                        result.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(ch))));
                return result;
            };
            const auto requested = normalize(token.text);
            std::int16_t match = -1;
            bool ambiguous = false;
            for (const auto eopid : CidOptionIds()[static_cast<std::size_t>(selectedCid_)])
            {
                const auto *propertyName = PropertyNameForCid(selectedCid_, eopid);
                if (propertyName && normalize(propertyName) == requested)
                {
                    if (match >= 0 && match != eopid)
                        ambiguous = true;
                    match = eopid;
                }
            }
            if (ambiguous)
            {
                Error(token, "Property name '" + token.text + "' is ambiguous for this CID; use its numeric EOPID.");
                return -1;
            }
            if (match >= 0)
            {
                const auto node = AddNode(kS32);
                if (node >= 0)
                {
                    output_->nodes[static_cast<std::size_t>(node)].intValue = match;
                    output_->cidResolvedPropertyNodes.insert(node);
                }
                return node;
            }
        }
        if (token.text.size() > 1 && token.text.front() == '$')
        {
            const auto name = token.text.substr(1);
            if (!std::isalpha(static_cast<unsigned char>(name.front())) && name.front() != '_')
            {
                Error(token, "Local symbol names must begin with a letter or underscore.");
                return -1;
            }
            if (!std::all_of(name.begin(), name.end(),
                             [](const char ch) { return std::isalnum(static_cast<unsigned char>(ch)) || ch == '_'; }))
            {
                Error(token, "Local symbol names may contain only letters, digits, and underscores.");
                return -1;
            }
            auto [found, inserted] = localSymbols_.try_emplace(name, nextSymbol_);
            if (inserted)
            {
                if (nextSymbol_ == 1'000'000u)
                {
                    Error(token, "Local symbol allocation exceeds ProjectCane's safe symbol limit.");
                    return -1;
                }
                ++nextSymbol_;
            }
            const auto node = AddNode(kSymid);
            if (node >= 0)
                output_->nodes[static_cast<std::size_t>(node)].intValue = static_cast<std::int32_t>(found->second);
            return node;
        }
        Error(token, "Unknown Splice word '" + token.text + "'. Use sym#N for unresolved symbols.");
        return -1;
    }

    const std::string &source_;
    std::int16_t selectedCid_ = -1;
    const BrxDocument *editorAliases_ = nullptr;
    std::size_t position_ = 0;
    std::uint32_t line_ = 1, column_ = 1;
    Token token_;
    Token expressionOrigin_;
    std::unordered_map<std::string, std::uint32_t> localSymbols_;
    std::uint32_t nextSymbol_ = 0;
    ParsedSpliceSource *output_ = nullptr;
};

void ValidateParsedSpliceSemantics(ParsedSpliceSource &parsed, const std::int16_t selectedCid)
{
    if (parsed.root < 0 || static_cast<std::size_t>(parsed.root) >= parsed.nodes.size())
        return;
    // The BRX writer recurses through both child and next links. A flat source
    // list can exceed that bound even when its parenthesis nesting is shallow.
    std::vector<std::pair<std::int32_t, std::uint32_t>> serializationPending{{parsed.root, 0u}};
    while (!serializationPending.empty())
    {
        const auto [index, depth] = serializationPending.back();
        serializationPending.pop_back();
        if (index < 0 || static_cast<std::size_t>(index) >= parsed.nodes.size())
            continue;
        if (depth > 4096u)
        {
            parsed.result.diagnostics.push_back(
                {1, 1, "Source exceeds the editor serializer's 4096-step child/next traversal limit."});
            parsed.result.succeeded = false;
            return;
        }
        const auto &node = parsed.nodes[static_cast<std::size_t>(index)];
        serializationPending.emplace_back(node.childNode, depth + 1u);
        serializationPending.emplace_back(node.nextNode, depth + 1u);
    }
    std::size_t initializationBindings = 0;
    for (const auto expression : SpliceInitializationExpressions(parsed.nodes, parsed.root))
    {
        const auto &node = parsed.nodes[static_cast<std::size_t>(expression)];
        if (node.tag == kPair && node.childNode >= 0 &&
            static_cast<std::size_t>(node.childNode) < parsed.nodes.size() &&
            parsed.nodes[static_cast<std::size_t>(node.childNode)].tag == kDefine)
            ++initializationBindings;
    }
    if (initializationBindings > 64u)
        parsed.result.diagnostics.push_back(
            {1, 1, "Initialization exceeds ProjectCane's 64 frame binding slots, including nested begin blocks."});
    // Quoted data and surplus fixed-arity operands are not evaluated. Validate
    // their containing forms, but not apparent calls inside the ignored data.
    std::unordered_set<std::int32_t> unevaluatedData;
    const auto markData = [&](const std::int32_t root, const bool includeNext)
    {
        std::vector<std::pair<std::int32_t, bool>> pending{{root, includeNext}};
        while (!pending.empty())
        {
            const auto [index, siblings] = pending.back();
            pending.pop_back();
            if (index < 0 || static_cast<std::size_t>(index) >= parsed.nodes.size() ||
                !unevaluatedData.insert(index).second)
                continue;
            pending.emplace_back(parsed.nodes[static_cast<std::size_t>(index)].childNode, true);
            if (siblings)
                pending.emplace_back(parsed.nodes[static_cast<std::size_t>(index)].nextNode, true);
        }
    };
    for (const auto &node : parsed.nodes)
    {
        if (node.tag != kPair || node.childNode < 0 ||
            static_cast<std::size_t>(node.childNode) >= parsed.nodes.size())
            continue;
        const auto &head = parsed.nodes[static_cast<std::size_t>(node.childNode)];
        if (head.tag == kQuote)
            markData(head.nextNode, false);
        else if (head.tag == kBifk && head.intValue >= 0 && head.intValue < 125 &&
                 kBuiltinVariadic[head.intValue] == 0)
        {
            auto argument = head.nextNode;
            for (std::size_t index = 0; index < kBuiltinRequired[head.intValue] && argument >= 0 &&
                 static_cast<std::size_t>(argument) < parsed.nodes.size(); ++index)
                argument = parsed.nodes[static_cast<std::size_t>(argument)].nextNode;
            markData(argument, true);
        }
    }
    for (std::size_t index = 0; index < parsed.nodes.size(); ++index)
    {
        if (unevaluatedData.contains(static_cast<std::int32_t>(index)))
            continue;
        const auto location = index < parsed.nodeLocations.size() ? parsed.nodeLocations[index]
                                                                  : std::pair<std::uint32_t, std::uint32_t>{1, 1};
        const auto &expression = parsed.nodes[index];
        if (expression.tag != kPair || expression.childNode < 0 ||
            static_cast<std::size_t>(expression.childNode) >= parsed.nodes.size())
            continue;
        std::vector<std::int32_t> items;
        std::unordered_set<std::int32_t> visited;
        for (auto item = expression.childNode;
             item >= 0 && static_cast<std::size_t>(item) < parsed.nodes.size() && visited.insert(item).second;
             item = parsed.nodes[static_cast<std::size_t>(item)].nextNode)
            items.push_back(item);
        if (items.empty())
            continue;
        const auto &head = parsed.nodes[static_cast<std::size_t>(items.front())];
        std::size_t required = 0;
        const char *form = nullptr;
        if (head.tag == kSet)
        {
            required = 3;
            form = "set!";
        }
        else if (head.tag == kDefine)
        {
            required = 3;
            form = "define";
        }
        else if (head.tag == kImport)
        {
            required = 2;
            form = "import";
        }
        else if (head.tag == kAssert)
        {
            required = 2;
            form = "assert";
        }
        else if (head.tag == kQuote)
        {
            required = 2;
            form = "quote";
        }
        if (required != 0 && items.size() != required)
            parsed.result.diagnostics.push_back({location.first, location.second,
                                                 std::string(form) + " requires " + std::to_string(required - 1) +
                                                     " argument(s), but this expression has " +
                                                     std::to_string(items.size() - 1) + "."});
        if (head.tag == kBifk)
        {
            if (head.intValue < 0 || head.intValue >= 125)
                parsed.result.diagnostics.push_back(
                    {location.first, location.second,
                     "Built-in ID " + std::to_string(head.intValue) + " is outside ProjectCane's g_mpbifkbif table."});
            else
            {
                const auto argumentCount = items.size() - 1;
                const auto requiredArguments = static_cast<std::size_t>(kBuiltinRequired[head.intValue]);
                const bool variadic = kBuiltinVariadic[head.intValue] != 0;
                const bool validCount = argumentCount >= requiredArguments;
                // Optional operands occupy one packed list slot, not one slot
                // each. Fixed-arity calls ignore surplus expressions entirely.
                const auto operandSlots = requiredArguments + (variadic ? 1u : 0u);
                if (!validCount || operandSlots > 32)
                {
                    const std::string expectation = "at least " + std::to_string(requiredArguments);
                    parsed.result.diagnostics.push_back(
                        {location.first, location.second,
                         std::string(SpliceBuiltinName(head.intValue)) + " requires " + expectation +
                             " argument(s), but this expression has " + std::to_string(argumentCount) +
                             (operandSlots > 32 ? "; ProjectCane's evaluator has only 32 operand slots." : ".")});
                }
            }
        }
        if ((head.tag == kSet || head.tag == kDefine) && items.size() >= 2 &&
            parsed.nodes[static_cast<std::size_t>(items[1])].tag != kSymid)
            parsed.result.diagnostics.push_back(
                {location.first, location.second,
                 std::string(head.tag == kSet ? "set!" : "define") + " requires a symbol as its first argument."});
        if (head.tag == kImport && items.size() >= 2 && parsed.nodes[static_cast<std::size_t>(items[1])].tag != kS32)
            parsed.result.diagnostics.push_back(
                {location.first, location.second, "import requires an integer splice-module index."});
        if (head.tag == kIf && (items.size() < 3 || items.size() > 4))
            parsed.result.diagnostics.push_back(
                {location.first, location.second,
                 "if requires a condition, a true expression, and at most one false expression."});
        if (head.tag == kLambda)
        {
            if (items.size() < 2)
                parsed.result.diagnostics.push_back(
                    {location.first, location.second, "lambda requires a formal-argument list."});
            else if (parsed.nodes[static_cast<std::size_t>(items[1])].tag != kPair)
                parsed.result.diagnostics.push_back({location.first, location.second,
                                                     "lambda formals must be written as a list, for example ($self)."});
        }
        // Native object-option and find-object built-ins evaluate their
        // operands and coerce the resulting CRef. Symbols and calls are valid
        // OIDs/EOPIDs/values, not evidence of a literal type mismatch.
        if (head.tag == kBifk && (head.intValue == 101 || head.intValue == 102) && items.size() >= 3 &&
            parsed.nodes[static_cast<std::size_t>(items[2])].tag == kS32 &&
            parsed.cidResolvedPropertyNodes.contains(items[2]) && selectedCid >= 0 &&
            static_cast<std::size_t>(selectedCid) < CidOptionIds().size())
        {
            const auto eopid = static_cast<std::int16_t>(parsed.nodes[static_cast<std::size_t>(items[2])].intValue);
            const auto &legal = CidOptionIds()[static_cast<std::size_t>(selectedCid)];
            if (std::find(legal.begin(), legal.end(), eopid) == legal.end())
                parsed.result.diagnostics.push_back(
                    {location.first, location.second,
                     "EOPID " + std::to_string(eopid) + " is not legal for the selected object's CID."});
        }
    }
    parsed.result.succeeded = parsed.root >= 0 && parsed.result.diagnostics.empty();
}
}

SpliceSourceResult ValidateSpliceModuleSource(const std::string &source, const std::int16_t selectedCid,
                                              const std::uint32_t symbolBase, const BrxDocument *editorAliases)
{
    auto parsed = SpliceSourceParser(source, selectedCid, symbolBase, editorAliases).Parse();
    ValidateParsedSpliceSemantics(parsed, selectedCid);
    return parsed.result;
}

SpliceSourceResult ApplySpliceModuleSource(BrxDocument &document, const std::int32_t spliceIndex,
                                           const std::string &source, const std::int16_t selectedCid)
{
    auto parsed = SpliceSourceParser(source, selectedCid, document.WorldHeader().symbolCount, &document).Parse();
    ValidateParsedSpliceSemantics(parsed, selectedCid);
    if (!parsed.result.succeeded)
        return parsed.result;
    if (spliceIndex < 0 || static_cast<std::size_t>(spliceIndex) >= document.SpliceModules().size())
    {
        parsed.result.succeeded = false;
        parsed.result.diagnostics.push_back({1, 1, "The target splice module no longer exists."});
        return parsed.result;
    }
    if (document.SpliceNodes().size() > static_cast<std::size_t>(INT32_MAX))
    {
        parsed.result.succeeded = false;
        parsed.result.diagnostics.push_back(
            {1, 1, "The existing splice-node table already exceeds the source editor's index limit."});
        return parsed.result;
    }
    const auto base = static_cast<std::int32_t>(document.SpliceNodes().size());
    if (parsed.nodes.size() > static_cast<std::size_t>(INT32_MAX - base))
    {
        parsed.result.succeeded = false;
        parsed.result.diagnostics.push_back({1, 1, "Applying the source would exceed the node-index limit."});
        return parsed.result;
    }
    // Applying replaces one module and then compacts. Count surviving nodes,
    // not the temporary old+new table, so valid replacements at the limit work.
    std::vector<std::uint8_t> retained(document.SpliceNodes().size(), 0);
    std::vector<std::int32_t> pending;
    for (std::size_t index = 0; index < document.SpliceModules().size(); ++index)
        if (index != static_cast<std::size_t>(spliceIndex))
            pending.push_back(document.SpliceModules()[index].rootNode);
    std::size_t retainedCount = 0;
    while (!pending.empty())
    {
        const auto index = pending.back();
        pending.pop_back();
        if (index == -1)
            continue;
        if (!ValidNode(document, index))
        {
            parsed.result.succeeded = false;
            parsed.result.diagnostics.push_back({1, 1,
                "Another module has an invalid expression link; applying source could rebind it to newly appended nodes."});
            return parsed.result;
        }
        if (retained[static_cast<std::size_t>(index)])
            continue;
        retained[static_cast<std::size_t>(index)] = 1;
        ++retainedCount;
        const auto &node = document.SpliceNodes()[static_cast<std::size_t>(index)];
        pending.push_back(node.childNode);
        pending.push_back(node.nextNode);
    }
    if (retainedCount > kMaximumSpliceNodes || parsed.nodes.size() > kMaximumSpliceNodes - retainedCount)
    {
        parsed.result.succeeded = false;
        parsed.result.diagnostics.push_back({1, 1, "Applying this source would exceed the loader's expression-node limit."});
        return parsed.result;
    }
    for (auto &node : parsed.nodes)
    {
        if (node.childNode >= 0)
            node.childNode += base;
        if (node.nextNode >= 0)
            node.nextNode += base;
        document.EditableSpliceNodes().push_back(node);
    }
    auto &module = document.EditableSpliceModules()[static_cast<std::size_t>(spliceIndex)];
    module.rootNode = parsed.root + base;
    module.modified = true;
    document.EditableWorldHeader().symbolCount =
        (std::max)(document.WorldHeader().symbolCount, parsed.result.requiredSymbolCount);
    CompactSpliceNodes(document);
    return parsed.result;
}

SpliceSourceResult VerifySpliceModuleSourceRoundTrip(const BrxDocument &document, const std::int32_t spliceIndex,
                                                     const std::int16_t selectedCid)
{
    SpliceSourceResult failed;
    if (spliceIndex < 0 || static_cast<std::size_t>(spliceIndex) >= document.SpliceModules().size())
    {
        failed.diagnostics.push_back({1, 1, "The splice module does not exist."});
        return failed;
    }
    auto parsed =
        SpliceSourceParser(FormatSpliceModuleSource(document, spliceIndex), selectedCid, 0, &document).Parse();
    ValidateParsedSpliceSemantics(parsed, selectedCid);
    if (!parsed.result.succeeded)
        return parsed.result;
    std::unordered_set<std::uint64_t> visited;
    std::function<bool(std::int32_t, std::int32_t)> equal =
        [&](const std::int32_t originalIndex, const std::int32_t parsedIndex)
    {
        if (originalIndex < 0 || parsedIndex < 0)
            return originalIndex == parsedIndex;
        if (static_cast<std::size_t>(originalIndex) >= document.SpliceNodes().size() ||
            static_cast<std::size_t>(parsedIndex) >= parsed.nodes.size())
            return false;
        const auto key = (static_cast<std::uint64_t>(static_cast<std::uint32_t>(originalIndex)) << 32) |
                         static_cast<std::uint32_t>(parsedIndex);
        if (!visited.insert(key).second)
            return true;
        const auto &a = document.SpliceNodes()[static_cast<std::size_t>(originalIndex)];
        const auto &b = parsed.nodes[static_cast<std::size_t>(parsedIndex)];
        if (a.tag != b.tag)
            return false;
        // Compare only payloads read by PpairSerializeIn. Raw tag edits can
        // leave inactive editor fields populated; they are not retail data.
        if (a.tag == kBool)
        {
            if ((a.intValue != 0) != (b.intValue != 0))
                return false;
        }
        else if ((a.tag == kS32 || a.tag == kSymid || a.tag == kBifk || a.tag == kBasic) &&
                 a.intValue != b.intValue)
            return false;
        const std::size_t floatCount = a.tag == kF32 ? 1u : a.tag == kMatrix ? 16u : a.tag == kLm ? 2u :
            (a.tag == kVector || a.tag == kClq || a.tag == kSmp) ? 3u : 0u;
        if (std::memcmp(a.floatValue, b.floatValue, floatCount * sizeof(float)) != 0)
            return false;
        return equal(a.childNode, b.childNode) && equal(a.nextNode, b.nextNode);
    };
    const auto originalRoot = document.SpliceModules()[static_cast<std::size_t>(spliceIndex)].rootNode;
    if (!equal(originalRoot, parsed.root))
    {
        parsed.result.succeeded = false;
        parsed.result.diagnostics.push_back(
            {1, 1,
             "Generated source parsed successfully but did not reproduce every tag, payload, child, and sibling link."});
    }
    return parsed.result;
}

SpliceModuleAnalysis AnalyzeSpliceModule(const BrxDocument &document, const std::int32_t spliceIndex)
{
    SpliceModuleAnalysis out;
    if (spliceIndex < 0 || static_cast<std::size_t>(spliceIndex) >= document.SpliceModules().size())
    {
        out.diagnostics.push_back("Splice index is outside the level splice map.");
        return out;
    }
    const auto moduleRoot = document.SpliceModules()[static_cast<std::size_t>(spliceIndex)].rootNode;
    if (moduleRoot != -1 && !ValidNode(document, moduleRoot))
        out.diagnostics.push_back("Module root does not reference a valid expression node.");
    if (ValidNode(document, moduleRoot) &&
        ValidNode(document, document.SpliceNodes()[static_cast<std::size_t>(moduleRoot)].nextNode))
        out.diagnostics.push_back(
            "The module root has sibling expressions; RefEvalModule executes only the first root expression.");
    std::unordered_set<std::int32_t> visited;
    std::unordered_set<std::int32_t> activeNodes;
    std::unordered_set<std::int32_t> visitedModules;
    std::unordered_set<std::int32_t> activeModules;
    std::unordered_map<std::uint32_t, std::size_t> eventDefinitionCounts;
    std::function<void(std::int32_t, std::uint32_t)> walk = [&](const std::int32_t index, const std::uint32_t depth)
    {
        if (!ValidNode(document, index))
            return;
        if (activeNodes.contains(index))
        {
            out.diagnostics.push_back("Expression graph contains a child/next cycle at node " + std::to_string(index) +
                                      ".");
            return;
        }
        if (!visited.insert(index).second)
        {
            out.diagnostics.push_back(
                "Expression node " + std::to_string(index) +
                " is reached by more than one child/next/root link. ProjectCane serializes expression trees, not shared-node graphs.");
            return;
        }
        activeNodes.insert(index);
        ++out.nodeCount;
        out.maximumDepth = (std::max)(out.maximumDepth, depth);
        const auto &node = document.SpliceNodes()[index];
        if (depth > 4096u)
        {
            out.diagnostics.push_back("Expression traversal exceeds the editor serializer's 4096-node recursion limit at node " +
                                      std::to_string(index) + ".");
            activeNodes.erase(index);
            return;
        }
        if (node.tag >= 36)
            out.diagnostics.push_back("Node " + std::to_string(index) + " uses invalid ProjectCane expression tag " +
                                      std::to_string(node.tag) + ".");
        if (node.childNode < -1 || (node.childNode >= 0 && !ValidNode(document, node.childNode)))
            out.diagnostics.push_back("Node " + std::to_string(index) + " has an invalid child link.");
        if (node.nextNode < -1 || (node.nextNode >= 0 && !ValidNode(document, node.nextNode)))
            out.diagnostics.push_back("Node " + std::to_string(index) + " has an invalid next link.");
        if (node.tag == kBifk)
        {
            out.builtinCalls.push_back(node.intValue);
            if (node.intValue < 0 || node.intValue >= 125)
                out.diagnostics.push_back("Node " + std::to_string(index) + " references invalid built-in " +
                                          std::to_string(node.intValue) + ".");
        }
        // A PAIR with a null child is the game's canonical empty list.  The
        // serializer writes its child as 0xFF and RefEval returns TAGK_Nil.
        // Do not reject () as a malformed call expression.
        if (node.tag != kPair && node.childNode != -1)
            out.diagnostics.push_back("Non-pair node " + std::to_string(index) +
                                      " has a child link that cannot be represented by ProjectCane's splice stream.");
        if (node.tag == kBasic)
        {
            out.basicTokens.push_back(node.intValue);
            if (node.intValue != 0)
                out.diagnostics.push_back(
                    "Node " + std::to_string(index) + " contains nonzero BASIC token " + std::to_string(node.intValue) +
                    "; ProjectCane deserializes it as a raw pointer. Use find-object with an OID instead.");
        }
        if (node.tag == kSymid &&
            (node.intValue < 0 || static_cast<std::uint32_t>(node.intValue) >= document.WorldHeader().symbolCount))
            out.diagnostics.push_back("Node " + std::to_string(index) + " references symbol " +
                                      std::to_string(node.intValue) + " outside symidMax " +
                                      std::to_string(document.WorldHeader().symbolCount) + ".");
        const auto finitePayload = [&](const std::size_t count)
        {
            return std::all_of(node.floatValue, node.floatValue + count,
                               [](const float value) { return std::isfinite(value); });
        };
        if ((node.tag == kF32 && !finitePayload(1)) ||
            ((node.tag == kVector || node.tag == kClq || node.tag == kSmp) && !finitePayload(3)) ||
            (node.tag == kMatrix && !finitePayload(16)) || (node.tag == kLm && !finitePayload(2)))
            out.diagnostics.push_back("Node " + std::to_string(index) +
                                      " contains a non-finite numeric payload that cannot be serialized.");
        if (node.tag == kPair && ValidNode(document, node.childNode))
        {
            const auto &head = document.SpliceNodes()[node.childNode];
            if (head.tag == kBifk && head.intValue >= 99 && head.intValue <= 102)
            {
                std::int32_t optionArg = head.nextNode;
                if (ValidNode(document, optionArg))
                    optionArg = document.SpliceNodes()[optionArg].nextNode;
                if (ValidNode(document, optionArg) && document.SpliceNodes()[optionArg].tag == kS32)
                {
                    const auto option = document.SpliceNodes()[optionArg].intValue;
                    (head.intValue == 102 ? out.optionReads : out.optionWrites).push_back(option);
                }
            }
        }
        walk(node.childNode, depth + 1);
        walk(node.nextNode, depth + 1);
        activeNodes.erase(index);
    };
    std::function<void(std::int32_t)> walkModule = [&](const std::int32_t moduleIndex)
    {
        if (moduleIndex < 0 || static_cast<std::size_t>(moduleIndex) >= document.SpliceModules().size())
            return;
        if (activeModules.contains(moduleIndex))
        {
            // RefEvalModule publishes its frame before evaluation, and CFrame
            // lookup tracks visited parents. Cyclic imports are representable.
            return;
        }
        if (!visitedModules.insert(moduleIndex).second)
            return;
        activeModules.insert(moduleIndex);
        const auto &module = document.SpliceModules()[static_cast<std::size_t>(moduleIndex)];
        std::vector<std::int32_t> moduleImports;
        std::size_t directDefinitions = 0;
        for (const auto expression : SpliceInitializationExpressions(document.SpliceNodes(), module.rootNode))
        {
            const auto &wrapper = document.SpliceNodes()[static_cast<std::size_t>(expression)];
            if (wrapper.tag != kPair || !ValidNode(document, wrapper.childNode))
                continue;
            const auto &head = document.SpliceNodes()[static_cast<std::size_t>(wrapper.childNode)];
            if (head.tag == kImport)
            {
                if (ValidNode(document, head.nextNode) &&
                    document.SpliceNodes()[static_cast<std::size_t>(head.nextNode)].tag == kS32)
                {
                    const auto imported = document.SpliceNodes()[static_cast<std::size_t>(head.nextNode)].intValue;
                    out.imports.push_back(imported);
                    moduleImports.push_back(imported);
                }
                else
                    out.diagnostics.push_back("Import expression at node " + std::to_string(expression) +
                                              " does not contain an integer splice index.");
            }
            if (head.tag == kDefine && ValidNode(document, head.nextNode))
            {
                ++directDefinitions;
                const auto &symbol = document.SpliceNodes()[static_cast<std::size_t>(head.nextNode)];
                if (symbol.tag == kSymid && symbol.intValue >= 0 && symbol.intValue <= 29)
                {
                    const auto eventId = static_cast<std::uint32_t>(symbol.intValue);
                    out.eventIds.push_back(eventId);
                    out.eventOrigins.emplace_back(eventId, moduleIndex);
                    ++eventDefinitionCounts[eventId];
                    if (!IsLambdaExpression(document, symbol.nextNode))
                        out.diagnostics.push_back(std::string("Event ") + SpliceEventName(eventId) +
                                                  " uses an indirect/computed value; static analysis cannot prove its callback type.");
                    else if (!IsRunnableEventLambda(document, symbol.nextNode))
                        out.diagnostics.push_back(
                            std::string("Event ") + SpliceEventName(eventId) +
                            " cannot run through ProjectCane's event dispatcher because it lacks a self formal or body expression.");
                }
            }
        }
        if (directDefinitions > 64)
            out.diagnostics.push_back("Module " + std::to_string(moduleIndex) + " defines " +
                                      std::to_string(directDefinitions) +
                                      " direct bindings, exceeding CFrame's 64-binding storage.");
        walk(module.rootNode, 0);
        // ProjectCane evaluates imports into the same lexical environment before
        // dispatching object callbacks. Analyze those modules too so an object's
        // effective event list matches what the runtime can actually resolve.
        const auto hasFrame = [&](const std::int32_t imported)
        {
            return imported >= 0 && static_cast<std::size_t>(imported) < document.SpliceModules().size() &&
                   ValidNode(document, document.SpliceModules()[static_cast<std::size_t>(imported)].rootNode);
        };
        const auto importedFrameCount = std::count_if(moduleImports.begin(), moduleImports.end(), hasFrame);
        if (importedFrameCount > 4)
            out.diagnostics.push_back("Module " + std::to_string(moduleIndex) + " imports " +
                                      std::to_string(importedFrameCount) +
                                      " parents; ProjectCane ignores imports after the fourth parent.");
        std::size_t parentCount = 0;
        for (const auto imported : moduleImports)
            if (hasFrame(imported) && parentCount++ < 4)
                walkModule(imported);
        activeModules.erase(moduleIndex);
    };
    walkModule(spliceIndex);
    const auto unique = [](auto &values)
    {
        std::sort(values.begin(), values.end());
        values.erase(std::unique(values.begin(), values.end()), values.end());
    };
    unique(out.eventIds);
    unique(out.imports);
    unique(out.builtinCalls);
    unique(out.optionReads);
    unique(out.optionWrites);
    unique(out.basicTokens);
    out.eventOrigins.erase(std::unique(out.eventOrigins.begin(), out.eventOrigins.end()), out.eventOrigins.end());
    for (const auto imported : out.imports)
        if (imported < 0 || static_cast<std::size_t>(imported) >= document.SpliceModules().size())
            out.diagnostics.push_back("Import references missing splice " + std::to_string(imported) + ".");
    for (const auto &[eventId, count] : eventDefinitionCounts)
        if (count > 1)
            out.diagnostics.push_back(
                std::string("Event ") + SpliceEventName(eventId) + " is defined " + std::to_string(count) +
                " times across the effective import chain; ProjectCane uses the most recent local binding, then imported parents in insertion order.");
    std::sort(out.diagnostics.begin(), out.diagnostics.end());
    out.diagnostics.erase(std::unique(out.diagnostics.begin(), out.diagnostics.end()), out.diagnostics.end());
    return out;
}

SpliceSandboxResult EvaluateSpliceEventSandbox(const BrxDocument &document, const std::int32_t spliceIndex,
                                               const std::uint32_t eventId, const std::int32_t selfObjectIndex,
                                               const std::uint32_t instructionLimit)
{
    Sandbox sandbox{document};
    sandbox.limit = (std::max)(instructionLimit, 1u);
    if (spliceIndex < 0 || static_cast<std::size_t>(spliceIndex) >= document.SpliceModules().size())
    {
        sandbox.result.diagnostics.push_back("Object has no valid splice module.");
        return sandbox.result;
    }
    sandbox.loadingModules.insert(spliceIndex);
    sandbox.EvalModuleBody(spliceIndex);
    sandbox.loadingModules.erase(spliceIndex);
    const auto binding = sandbox.bindings.find(eventId);
    if (binding == sandbox.bindings.end() || binding->second.kind != SandboxValue::Kind::Lambda)
    {
        sandbox.result.diagnostics.push_back("The module does not define this event callback.");
        return sandbox.result;
    }
    SandboxValue self;
    self.kind = SandboxValue::Kind::Object;
    self.integer = selfObjectIndex;
    const auto &lambda = document.SpliceNodes()[binding->second.node];
    const std::int32_t formals = lambda.nextNode;
    const auto first = ValidNode(document, formals) && document.SpliceNodes()[formals].tag == kPair
        ? document.SpliceNodes()[formals].childNode : -1;
    if (!ValidNode(document, first) || document.SpliceNodes()[first].tag != kSymid ||
        !ValidNode(document, document.SpliceNodes()[formals].nextNode))
    {
        sandbox.result.diagnostics.push_back("ProjectCane cannot dispatch this callback without self formals and a body.");
        return sandbox.result;
    }
    sandbox.bindings[static_cast<std::uint32_t>(document.SpliceNodes()[first].intValue)] = self;
    if (ValidNode(document, document.SpliceNodes()[first].nextNode))
        sandbox.Approximation("This callback declares event arguments. Preview supplies self only; parameter-dependent behavior is incomplete.");
    sandbox.CallLambda(binding->second.node, -1, true);
    sandbox.result.completed = sandbox.Running();
    sandbox.result.trace.insert(sandbox.result.trace.begin(), std::string("Event ") + SpliceEventName(eventId) +
                                                                  " on object record " +
                                                                  std::to_string(selfObjectIndex));
    return sandbox.result;
}

std::size_t SpliceModuleUserCount(const BrxDocument &document, const std::int32_t spliceIndex)
{
    if (spliceIndex < 0 || static_cast<std::size_t>(spliceIndex) >= document.SpliceModules().size())
        return 0;
    // Include potential users through literal imports, including imports in
    // callback bodies. A direct-object count misses changes to shared parents.
    std::unordered_map<std::int32_t, std::vector<std::int32_t>> dependents;
    for (std::size_t moduleIndex = 0; moduleIndex < document.SpliceModules().size(); ++moduleIndex)
    {
        std::vector<std::pair<std::int32_t, bool>> pending{
            {document.SpliceModules()[moduleIndex].rootNode, false}};
        std::unordered_set<std::int32_t> visited;
        while (!pending.empty())
        {
            const auto [index, includeNext] = pending.back();
            pending.pop_back();
            if (!ValidNode(document, index) || !visited.insert(index).second)
                continue;
            const auto &node = document.SpliceNodes()[static_cast<std::size_t>(index)];
            if (node.tag == kPair && ValidNode(document, node.childNode))
            {
                const auto &head = document.SpliceNodes()[static_cast<std::size_t>(node.childNode)];
                if (head.tag == kImport && ValidNode(document, head.nextNode))
                {
                    const auto &argument = document.SpliceNodes()[static_cast<std::size_t>(head.nextNode)];
                    if (argument.tag == kS32 && argument.intValue >= 0 &&
                        static_cast<std::size_t>(argument.intValue) < document.SpliceModules().size())
                        dependents[argument.intValue].push_back(static_cast<std::int32_t>(moduleIndex));
                }
            }
            if (node.tag == kPair && ValidNode(document, node.childNode) &&
                document.SpliceNodes()[static_cast<std::size_t>(node.childNode)].tag == kQuote)
            {
                // Imports appearing only in quoted data are never evaluated.
            }
            else
                pending.emplace_back(node.childNode, true);
            if (includeNext)
                pending.emplace_back(node.nextNode, true);
        }
    }
    std::unordered_set<std::int32_t> affectedModules{spliceIndex};
    std::vector<std::int32_t> pendingModules{spliceIndex};
    for (std::size_t index = 0; index < pendingModules.size(); ++index)
        if (const auto found = dependents.find(pendingModules[index]); found != dependents.end())
            for (const auto dependent : found->second)
                if (affectedModules.insert(dependent).second)
                    pendingModules.push_back(dependent);
    std::unordered_set<std::int32_t> users;
    for (std::size_t index = 0; index < document.Objects().size(); ++index)
        if (affectedModules.contains(document.Objects()[index].spliceIndex))
            users.insert(static_cast<std::int32_t>(index));
    for (const auto &proxy : document.Proxies())
        for (const auto &overrideRecord : proxy.overrides)
            if (overrideRecord.targetObjectIndex >= 0 &&
                static_cast<std::size_t>(overrideRecord.targetObjectIndex) < document.Objects().size() &&
                (affectedModules.contains(overrideRecord.inheritedSpliceIndex) ||
                 affectedModules.contains(overrideRecord.replacementSpliceIndex)))
                users.insert(overrideRecord.targetObjectIndex);
    return users.size();
}

namespace
{
bool ValidObject(const BrxDocument &document, const std::int32_t objectIndex)
{
    return objectIndex >= 0 && static_cast<std::size_t>(objectIndex) < document.Objects().size();
}

bool DirectSpliceBindingEditable(const BrxDocument &document, const std::int32_t objectIndex)
{
    if (document.Objects()[static_cast<std::size_t>(objectIndex)].proxyClone)
        return false;
    for (const auto &proxy : document.Proxies())
        for (const auto &overrideRecord : proxy.overrides)
            if (overrideRecord.targetObjectIndex == objectIndex)
                return false;
    return true;
}

std::int32_t AppendNode(BrxDocument &document, const std::uint8_t tag)
{
    BrxSpliceNode node;
    node.tag = tag;
    node.modified = true;
    node.editorCreated = true;
    document.EditableSpliceNodes().push_back(node);
    return static_cast<std::int32_t>(document.SpliceNodes().size() - 1);
}

std::int32_t BeginKeyword(const BrxDocument &document, const BrxSpliceModule &module)
{
    if (!ValidNode(document, module.rootNode))
        return -1;
    const auto &root = document.SpliceNodes()[static_cast<std::size_t>(module.rootNode)];
    if (root.tag != kPair || !ValidNode(document, root.childNode))
        return -1;
    return document.SpliceNodes()[static_cast<std::size_t>(root.childNode)].tag == kBegin ? root.childNode : -1;
}

std::int32_t EnsureBeginRoot(BrxDocument &document, BrxSpliceModule &module)
{
    if (const auto begin = BeginKeyword(document, module); begin >= 0)
        return begin;
    const auto previousRoot = module.rootNode;
    if (ValidNode(document, previousRoot) &&
        ValidNode(document, document.SpliceNodes()[static_cast<std::size_t>(previousRoot)].nextNode))
        return -1;
    const auto root = AppendNode(document, kPair);
    const auto begin = AppendNode(document, kBegin);
    auto &nodes = document.EditableSpliceNodes();
    nodes[static_cast<std::size_t>(root)].childNode = begin;
    nodes[static_cast<std::size_t>(begin)].nextNode = previousRoot;
    module.rootNode = root;
    module.modified = true;
    return begin;
}

std::int32_t TopLevelExpressionHead(const BrxDocument &document, const BrxSpliceModule &module)
{
    if (const auto begin = BeginKeyword(document, module); begin >= 0)
        return document.SpliceNodes()[static_cast<std::size_t>(begin)].nextNode;
    return module.rootNode;
}

std::int32_t DirectEventId(const BrxDocument &document, const std::int32_t expression)
{
    if (!ValidNode(document, expression))
        return -1;
    const auto &wrapper = document.SpliceNodes()[static_cast<std::size_t>(expression)];
    if (wrapper.tag != kPair || !ValidNode(document, wrapper.childNode))
        return -1;
    const auto &define = document.SpliceNodes()[static_cast<std::size_t>(wrapper.childNode)];
    if (define.tag != kDefine || !ValidNode(document, define.nextNode))
        return -1;
    const auto &symbol = document.SpliceNodes()[static_cast<std::size_t>(define.nextNode)];
    return symbol.tag == kSymid && symbol.intValue >= 0 && symbol.intValue <= 29 ? symbol.intValue : -1;
}

std::int32_t LatestDirectEvent(const BrxDocument &document, const BrxSpliceModule &module,
                               const std::uint32_t eventId)
{
    if (eventId > 29u)
        return -1;
    std::int32_t latest = -1;
    for (const auto expression : SpliceInitializationExpressions(document.SpliceNodes(), module.rootNode))
    {
        if (DirectEventId(document, expression) == static_cast<std::int32_t>(eventId))
            latest = expression;
        else
        {
            const auto &wrapper = document.SpliceNodes()[static_cast<std::size_t>(expression)];
            if (wrapper.tag == kPair && ValidNode(document, wrapper.childNode))
            {
                const auto &head = document.SpliceNodes()[static_cast<std::size_t>(wrapper.childNode)];
                if (head.tag == kSet && ValidNode(document, head.nextNode))
                {
                    const auto &symbol = document.SpliceNodes()[static_cast<std::size_t>(head.nextNode)];
                    if (symbol.tag == kSymid && symbol.intValue == static_cast<std::int32_t>(eventId))
                        latest = -1; // A later assignment controls the callback; do not edit a shadowed define.
                }
            }
        }
    }
    return latest;
}

// Keep a module frame alive when its only initialization expression is removed.
// Promoting a root sibling would execute data that retail previously ignored.
bool UnlinkInitializationExpression(BrxDocument &document, BrxSpliceModule &module,
                                    const std::int32_t expression)
{
    if (!ValidNode(document, expression))
        return false;
    auto &nodes = document.EditableSpliceNodes();
    if (module.rootNode == expression)
    {
        const auto headIndex = nodes[static_cast<std::size_t>(expression)].childNode;
        if (nodes[static_cast<std::size_t>(expression)].tag != kPair || !ValidNode(document, headIndex))
            return false;
        auto &head = nodes[static_cast<std::size_t>(headIndex)];
        head.tag = kBegin;
        head.childNode = -1;
        head.nextNode = -1;
        head.modified = true;
    }
    else
    {
        std::int32_t predecessor = -1;
        std::vector<std::int32_t> pending{module.rootNode};
        std::unordered_set<std::int32_t> visited;
        while (!pending.empty())
        {
            const auto index = pending.back();
            pending.pop_back();
            if (!ValidNode(document, index) || !visited.insert(index).second)
                continue;
            const auto &node = nodes[static_cast<std::size_t>(index)];
            if (node.nextNode == expression)
            {
                if (predecessor >= 0)
                    return false; // Ambiguous shared graph: leave it untouched.
                predecessor = index;
            }
            pending.push_back(node.childNode);
            pending.push_back(node.nextNode);
        }
        if (predecessor < 0)
            return false;
        nodes[static_cast<std::size_t>(predecessor)].nextNode =
            nodes[static_cast<std::size_t>(expression)].nextNode;
        nodes[static_cast<std::size_t>(predecessor)].modified = true;
    }
    module.modified = true;
    return true;
}
}

SpliceAuthoringResult CreateSpliceModule(BrxDocument &document, const std::int32_t objectIndex)
{
    SpliceAuthoringResult result;
    if (!ValidObject(document, objectIndex))
    {
        result.message = "Select a valid object first.";
        return result;
    }
    if (!DirectSpliceBindingEditable(document, objectIndex))
    {
        result.message = "This Splice binding is controlled by a PROXY. Use its override controls or a unique source.";
        return result;
    }
    if (document.SpliceModules().size() > static_cast<std::size_t>(INT16_MAX) ||
        document.SpliceNodes().size() > 9'999'998)
    {
        result.message =
            "Creating an attached splice module would exceed the object's signed 16-bit splice-index field.";
        return result;
    }
    BrxSpliceModule module;
    module.modified = true;
    module.editorCreated = true;
    document.EditableSpliceModules().push_back(module);
    result.moduleIndex = static_cast<std::int32_t>(document.SpliceModules().size() - 1);
    auto &createdModule = document.EditableSpliceModules()[static_cast<std::size_t>(result.moduleIndex)];
    EnsureBeginRoot(document, createdModule);
    auto &object = document.EditableObjects()[static_cast<std::size_t>(objectIndex)];
    object.spliceIndex = result.moduleIndex;
    object.spliceModified = true;
    result.succeeded = true;
    result.message = "Created and attached an empty splice module in memory.";
    return result;
}

SpliceAuthoringResult AttachSpliceModule(BrxDocument &document, const std::int32_t objectIndex,
                                         const std::int32_t spliceIndex)
{
    SpliceAuthoringResult result;
    if (!ValidObject(document, objectIndex))
    {
        result.message = "Select a valid object first.";
        return result;
    }
    if (!DirectSpliceBindingEditable(document, objectIndex))
    {
        result.message = "This Splice binding is controlled by a PROXY. Use its override controls or a unique source.";
        return result;
    }
    if (spliceIndex < 0 || static_cast<std::size_t>(spliceIndex) >= document.SpliceModules().size())
    {
        result.message = "The selected splice module does not exist.";
        return result;
    }
    if (spliceIndex > INT16_MAX)
    {
        result.message =
            "Objects can only attach splice modules 0 through 32767 because the BRX object header stores a signed 16-bit index.";
        return result;
    }
    auto &object = document.EditableObjects()[static_cast<std::size_t>(objectIndex)];
    object.spliceIndex = spliceIndex;
    object.spliceModified = true;
    result.succeeded = true;
    result.moduleIndex = spliceIndex;
    result.message = "Attached existing splice module " + std::to_string(spliceIndex) + ".";
    return result;
}

SpliceAuthoringResult DetachSpliceModule(BrxDocument &document, const std::int32_t objectIndex)
{
    SpliceAuthoringResult result;
    if (!ValidObject(document, objectIndex))
    {
        result.message = "Select a valid object first.";
        return result;
    }
    if (!DirectSpliceBindingEditable(document, objectIndex))
    {
        result.message = "This Splice binding is controlled by a PROXY. Use its override controls or a unique source.";
        return result;
    }
    auto &object = document.EditableObjects()[static_cast<std::size_t>(objectIndex)];
    object.spliceIndex = -1;
    object.spliceModified = true;
    result.succeeded = true;
    result.message = "Detached the object's splice module in memory.";
    return result;
}

SpliceAuthoringResult MakeSpliceModuleUnique(BrxDocument &document, const std::int32_t objectIndex)
{
    SpliceAuthoringResult result;
    if (!ValidObject(document, objectIndex))
    {
        result.message = "Select a valid object first.";
        return result;
    }
    if (!DirectSpliceBindingEditable(document, objectIndex))
    {
        result.message = "This Splice binding is controlled by a PROXY. Use its override controls or a unique source.";
        return result;
    }
    const auto sourceIndex = document.Objects()[static_cast<std::size_t>(objectIndex)].spliceIndex;
    if (sourceIndex < 0 || static_cast<std::size_t>(sourceIndex) >= document.SpliceModules().size())
    {
        result.message = "The object has no valid splice module to duplicate.";
        return result;
    }
    if (document.SpliceModules().size() > static_cast<std::size_t>(INT16_MAX))
    {
        result.message = "Duplicating this attached module would exceed the object's signed 16-bit splice-index field.";
        return result;
    }
    std::unordered_set<std::int32_t> sourceNodes;
    std::vector<std::int32_t> pendingNodes{document.SpliceModules()[static_cast<std::size_t>(sourceIndex)].rootNode};
    while (!pendingNodes.empty())
    {
        const auto node = pendingNodes.back();
        pendingNodes.pop_back();
        if (node == -1)
            continue;
        if (!ValidNode(document, node))
        {
            result.message = "The module contains an invalid expression link; repair it before duplicating.";
            return result;
        }
        if (!sourceNodes.insert(node).second)
            continue;
        pendingNodes.push_back(document.SpliceNodes()[static_cast<std::size_t>(node)].childNode);
        pendingNodes.push_back(document.SpliceNodes()[static_cast<std::size_t>(node)].nextNode);
    }
    if (document.SpliceNodes().size() > 10'000'000 || sourceNodes.size() > 10'000'000 - document.SpliceNodes().size())
    {
        result.message = "Duplicating the module would exceed the loader's expression-node limit.";
        return result;
    }
    // Allocate all copies before reconnecting links: flat sibling chains and
    // cyclic malformed graphs must not recurse through the host C++ stack.
    std::vector<std::int32_t> ordered(sourceNodes.begin(), sourceNodes.end());
    std::sort(ordered.begin(), ordered.end());
    std::unordered_map<std::int32_t, std::int32_t> remap;
    for (const auto sourceNode : ordered)
    {
        auto copy = document.SpliceNodes()[static_cast<std::size_t>(sourceNode)];
        copy.offset = 0;
        copy.serializedSize = 0;
        copy.modified = true;
        copy.editorCreated = true;
        remap[sourceNode] = static_cast<std::int32_t>(document.SpliceNodes().size());
        document.EditableSpliceNodes().push_back(copy);
    }
    const auto mapped = [&](const std::int32_t index) { return index == -1 ? -1 : remap.at(index); };
    for (const auto sourceNode : ordered)
    {
        const auto source = document.SpliceNodes()[static_cast<std::size_t>(sourceNode)];
        auto &target = document.EditableSpliceNodes()[static_cast<std::size_t>(remap.at(sourceNode))];
        target.childNode = mapped(source.childNode);
        target.nextNode = mapped(source.nextNode);
    }
    BrxSpliceModule module;
    module.rootNode = mapped(document.SpliceModules()[static_cast<std::size_t>(sourceIndex)].rootNode);
    module.modified = true;
    module.editorCreated = true;
    document.EditableSpliceModules().push_back(module);
    result.moduleIndex = static_cast<std::int32_t>(document.SpliceModules().size() - 1);
    auto &object = document.EditableObjects()[static_cast<std::size_t>(objectIndex)];
    object.spliceIndex = result.moduleIndex;
    object.spliceModified = true;
    result.succeeded = true;
    result.message = "Duplicated shared splice " + std::to_string(sourceIndex) + " as unique module " +
                     std::to_string(result.moduleIndex) + ".";
    return result;
}

SpliceAuthoringResult AddSpliceEventStub(BrxDocument &document, const std::int32_t objectIndex,
                                         const std::uint32_t eventId)
{
    SpliceAuthoringResult result;
    if (eventId > 29)
    {
        result.message = "The event ID is outside ProjectCane's recognized callback range.";
        return result;
    }
    if (!ValidObject(document, objectIndex))
    {
        result.message = "Select a valid object first.";
        return result;
    }
    const auto selfSymbol = (std::max)(document.WorldHeader().symbolCount, 30u);
    if (selfSymbol >= 1'000'000u)
    {
        result.message = "The level symbol table is already at the loader's safe limit.";
        return result;
    }
    if (document.SpliceNodes().size() > 9'999'990)
    {
        result.message = "Adding a callback would exceed the loader's expression-node limit.";
        return result;
    }
    auto moduleIndex = document.Objects()[static_cast<std::size_t>(objectIndex)].spliceIndex;
    if (moduleIndex < 0 || static_cast<std::size_t>(moduleIndex) >= document.SpliceModules().size())
    {
        result = CreateSpliceModule(document, objectIndex);
        if (!result.succeeded)
            return result;
        moduleIndex = result.moduleIndex;
    }
    const auto analysis = AnalyzeSpliceModule(document, moduleIndex);
    const auto &existingModule = document.SpliceModules()[static_cast<std::size_t>(moduleIndex)];
    std::size_t directDefinitions = 0;
    for (const auto expression : SpliceInitializationExpressions(document.SpliceNodes(), existingModule.rootNode))
    {
        const auto &expressionNode = document.SpliceNodes()[static_cast<std::size_t>(expression)];
        if (expressionNode.tag == kPair && ValidNode(document, expressionNode.childNode) &&
            document.SpliceNodes()[static_cast<std::size_t>(expressionNode.childNode)].tag == kDefine)
            ++directDefinitions;
        if (DirectEventId(document, expression) == static_cast<std::int32_t>(eventId))
        {
            result.message = "That event is already defined directly by this module.";
            return result;
        }
    }
    if (directDefinitions >= 64)
    {
        result.message = "The module already uses all 64 ProjectCane frame binding slots.";
        return result;
    }
    const bool overridesImport =
        std::find(analysis.eventIds.begin(), analysis.eventIds.end(), eventId) != analysis.eventIds.end();
    auto &module = document.EditableSpliceModules()[static_cast<std::size_t>(moduleIndex)];
    const auto begin = EnsureBeginRoot(document, module);
    if (begin < 0)
    {
        result.message =
            "The module has sibling root expressions that ProjectCane does not execute; repair that structure before adding callbacks.";
        return result;
    }

    const auto expression = AppendNode(document, kPair);
    const auto define = AppendNode(document, kDefine);
    const auto symbol = AppendNode(document, kSymid);
    const auto lambdaExpression = AppendNode(document, kPair);
    const auto lambda = AppendNode(document, kLambda);
    const auto formals = AppendNode(document, kPair);
    const auto selfFormal = AppendNode(document, kSymid);
    const auto safeBody = AppendNode(document, kBool);
    auto &nodes = document.EditableSpliceNodes();
    nodes[static_cast<std::size_t>(expression)].childNode = define;
    nodes[static_cast<std::size_t>(define)].nextNode = symbol;
    nodes[static_cast<std::size_t>(symbol)].intValue = static_cast<std::int32_t>(eventId);
    nodes[static_cast<std::size_t>(symbol)].nextNode = lambdaExpression;
    nodes[static_cast<std::size_t>(lambdaExpression)].childNode = lambda;
    nodes[static_cast<std::size_t>(lambda)].nextNode = formals;
    nodes[static_cast<std::size_t>(formals)].childNode = selfFormal;
    nodes[static_cast<std::size_t>(formals)].nextNode = safeBody;
    nodes[static_cast<std::size_t>(selfFormal)].intValue = static_cast<std::int32_t>(selfSymbol);
    nodes[static_cast<std::size_t>(safeBody)].intValue = 1;
    document.EditableWorldHeader().symbolCount = selfSymbol + 1;
    std::int32_t tail = begin;
    std::unordered_set<std::int32_t> visited;
    while (ValidNode(document, nodes[static_cast<std::size_t>(tail)].nextNode) && visited.insert(tail).second)
        tail = nodes[static_cast<std::size_t>(tail)].nextNode;
    nodes[static_cast<std::size_t>(tail)].nextNode = expression;
    nodes[static_cast<std::size_t>(tail)].modified = true;
    module.modified = true;
    result.succeeded = true;
    result.moduleIndex = moduleIndex;
    result.message =
        std::string("Added runnable safe ") + SpliceEventName(eventId) +
        (overridesImport ? " callback overriding the imported definition in memory." : " callback in memory.");
    return result;
}

SpliceAuthoringResult RemoveSpliceEvent(BrxDocument &document, const std::int32_t objectIndex,
                                        const std::uint32_t eventId)
{
    SpliceAuthoringResult result;
    if (!ValidObject(document, objectIndex))
    {
        result.message = "Select a valid object first.";
        return result;
    }
    const auto moduleIndex = document.Objects()[static_cast<std::size_t>(objectIndex)].spliceIndex;
    if (moduleIndex < 0 || static_cast<std::size_t>(moduleIndex) >= document.SpliceModules().size())
    {
        result.message = "The object has no valid splice module.";
        return result;
    }
    auto &module = document.EditableSpliceModules()[static_cast<std::size_t>(moduleIndex)];
    const auto effectiveEvent = LatestDirectEvent(document, module, eventId);
    if (UnlinkInitializationExpression(document, module, effectiveEvent))
    {
        result.succeeded = true;
        result.moduleIndex = moduleIndex;
        CompactSpliceNodes(document);
        result.message = std::string("Removed the latest direct ") + SpliceEventName(eventId) +
                         " binding. An older or imported binding may now take effect.";
        return result;
    }
    result.message = "The event comes from an import or is not directly defined, so it was not removed.";
    return result;
}

SpliceAuthoringResult RepairSpliceImports(BrxDocument &document, const std::int32_t spliceIndex)
{
    SpliceAuthoringResult result;
    if (spliceIndex < 0 || static_cast<std::size_t>(spliceIndex) >= document.SpliceModules().size())
    {
        result.message = "Select a valid splice module first.";
        return result;
    }
    auto &module = document.EditableSpliceModules()[static_cast<std::size_t>(spliceIndex)];
    auto &nodes = document.EditableSpliceNodes();
    std::size_t removed = 0;
    for (const auto expression : SpliceInitializationExpressions(document.SpliceNodes(), module.rootNode))
    {
        bool invalidImport = false;
        const auto &wrapper = nodes[static_cast<std::size_t>(expression)];
        if (wrapper.tag == kPair && ValidNode(document, wrapper.childNode))
        {
            const auto &head = nodes[static_cast<std::size_t>(wrapper.childNode)];
            if (head.tag == kImport)
            {
                invalidImport =
                    !ValidNode(document, head.nextNode) || nodes[static_cast<std::size_t>(head.nextNode)].tag != kS32;
                if (!invalidImport)
                {
                    const auto imported = nodes[static_cast<std::size_t>(head.nextNode)].intValue;
                    invalidImport =
                        imported < 0 || static_cast<std::size_t>(imported) >= document.SpliceModules().size();
                    // Duplicate, self and cyclic parent links are allowed by
                    // CFrame. Keep valid imports in place, even after slot four;
                    // removing them can change which later parents are active.
                }
            }
        }
        if (invalidImport && UnlinkInitializationExpression(document, module, expression))
            ++removed;
    }
    module.modified |= removed != 0;
    if (removed != 0)
        CompactSpliceNodes(document);
    result.succeeded = removed != 0;
    result.moduleIndex = spliceIndex;
    result.message = removed == 0 ? "No invalid direct imports needed repair."
                                  : "Removed " + std::to_string(removed) + " invalid direct import(s) in memory.";
    return result;
}

SpliceAuthoringResult AppendSpliceTopLevelNode(BrxDocument &document, const std::int32_t spliceIndex,
                                               const std::uint8_t tag)
{
    SpliceAuthoringResult result;
    if (spliceIndex < 0 || static_cast<std::size_t>(spliceIndex) >= document.SpliceModules().size())
    {
        result.message = "Select a valid splice module first.";
        return result;
    }
    if (tag >= 36u)
    {
        result.message = "The requested ProjectCane expression tag is invalid.";
        return result;
    }
    const auto &originalModule = document.SpliceModules()[static_cast<std::size_t>(spliceIndex)];
    const std::size_t addedNodes = BeginKeyword(document, originalModule) >= 0 ? 1u : 3u;
    if (!CanAppendSpliceNodes(document, addedNodes))
    {
        result.message = "Adding an expression would exceed the loader's safe node limit.";
        return result;
    }
    auto &module = document.EditableSpliceModules()[static_cast<std::size_t>(spliceIndex)];
    const auto begin = EnsureBeginRoot(document, module);
    if (begin < 0)
    {
        result.message = "Repair the module root before appending expressions.";
        return result;
    }
    const auto expression = AppendNode(document, tag);
    auto &nodes = document.EditableSpliceNodes();
    std::int32_t tail = begin;
    std::unordered_set<std::int32_t> visited;
    while (ValidNode(document, nodes[static_cast<std::size_t>(tail)].nextNode) && visited.insert(tail).second)
        tail = nodes[static_cast<std::size_t>(tail)].nextNode;
    nodes[static_cast<std::size_t>(tail)].nextNode = expression;
    nodes[static_cast<std::size_t>(tail)].modified = true;
    module.modified = true;
    result.succeeded = true;
    result.moduleIndex = spliceIndex;
    result.message = "Appended a reachable top-level splice expression node.";
    return result;
}

SpliceAuthoringResult AddSpliceImport(BrxDocument &document, const std::int32_t spliceIndex,
                                      const std::int32_t importedModuleIndex)
{
    SpliceAuthoringResult result;
    if (spliceIndex < 0 || static_cast<std::size_t>(spliceIndex) >= document.SpliceModules().size() ||
        importedModuleIndex < 0 || static_cast<std::size_t>(importedModuleIndex) >= document.SpliceModules().size())
    {
        result.message = "Both the destination and imported splice modules must exist.";
        return result;
    }
    std::vector<std::int32_t> directImports;
    const auto &destination = document.SpliceModules()[static_cast<std::size_t>(spliceIndex)];
    for (const auto expression : SpliceInitializationExpressions(document.SpliceNodes(), destination.rootNode))
    {
        const auto &wrapper = document.SpliceNodes()[static_cast<std::size_t>(expression)];
        if (wrapper.tag == kPair && ValidNode(document, wrapper.childNode))
        {
            const auto &head = document.SpliceNodes()[static_cast<std::size_t>(wrapper.childNode)];
            if (head.tag == kImport && ValidNode(document, head.nextNode) &&
                document.SpliceNodes()[static_cast<std::size_t>(head.nextNode)].tag == kS32)
            {
                const auto imported = document.SpliceNodes()[static_cast<std::size_t>(head.nextNode)].intValue;
                if (imported >= 0 && static_cast<std::size_t>(imported) < document.SpliceModules().size() &&
                    ValidNode(document, document.SpliceModules()[static_cast<std::size_t>(imported)].rootNode))
                    directImports.push_back(imported);
            }
        }
    }
    if (directImports.size() >= 4 &&
        ValidNode(document, document.SpliceModules()[static_cast<std::size_t>(importedModuleIndex)].rootNode))
    {
        result.message = "ProjectCane supports at most four imported parent frames per module.";
        return result;
    }
    const std::size_t addedNodes = BeginKeyword(document, destination) >= 0 ? 3u : 5u;
    if (!CanAppendSpliceNodes(document, addedNodes))
    {
        result.message = "Adding the import would exceed the loader's expression-node limit.";
        return result;
    }
    auto &module = document.EditableSpliceModules()[static_cast<std::size_t>(spliceIndex)];
    const auto begin = EnsureBeginRoot(document, module);
    if (begin < 0)
    {
        result.message = "Repair the module root before adding an import.";
        return result;
    }
    const auto expression = AppendNode(document, kPair);
    const auto import = AppendNode(document, kImport);
    const auto moduleLiteral = AppendNode(document, kS32);
    auto &nodes = document.EditableSpliceNodes();
    nodes[static_cast<std::size_t>(expression)].childNode = import;
    nodes[static_cast<std::size_t>(import)].nextNode = moduleLiteral;
    nodes[static_cast<std::size_t>(moduleLiteral)].intValue = importedModuleIndex;
    std::int32_t tail = begin;
    std::unordered_set<std::int32_t> visited;
    while (ValidNode(document, nodes[static_cast<std::size_t>(tail)].nextNode) && visited.insert(tail).second)
        tail = nodes[static_cast<std::size_t>(tail)].nextNode;
    nodes[static_cast<std::size_t>(tail)].nextNode = expression;
    nodes[static_cast<std::size_t>(tail)].modified = true;
    module.modified = true;
    result.succeeded = true;
    result.moduleIndex = spliceIndex;
    result.message = "Imported splice module " + std::to_string(importedModuleIndex) + ".";
    return result;
}

SpliceAuthoringResult SetSpliceEventBooleanResult(BrxDocument &document, const std::int32_t objectIndex,
                                                  const std::uint32_t eventId, const bool value)
{
    SpliceAuthoringResult result;
    if (!ValidObject(document, objectIndex))
    {
        result.message = "Select a valid object first.";
        return result;
    }
    const auto moduleIndex = document.Objects()[static_cast<std::size_t>(objectIndex)].spliceIndex;
    if (moduleIndex < 0 || static_cast<std::size_t>(moduleIndex) >= document.SpliceModules().size())
    {
        result.message = "The object has no valid splice module.";
        return result;
    }
    auto &module = document.EditableSpliceModules()[static_cast<std::size_t>(moduleIndex)];
    const auto effectiveEvent = LatestDirectEvent(document, module, eventId);
    for (const auto expression : SpliceInitializationExpressions(document.SpliceNodes(), module.rootNode))
    {
        if (expression != effectiveEvent)
            continue;
        const auto &wrapper = document.SpliceNodes()[static_cast<std::size_t>(expression)];
        const auto define = wrapper.childNode;
        const auto symbol = document.SpliceNodes()[static_cast<std::size_t>(define)].nextNode;
        const auto lambdaExpression = document.SpliceNodes()[static_cast<std::size_t>(symbol)].nextNode;
        if (!IsRunnableEventLambda(document, lambdaExpression))
            break;
        const auto lambda = document.SpliceNodes()[static_cast<std::size_t>(lambdaExpression)].childNode;
        if (!ValidNode(document, lambda))
            break;
        const auto formals = document.SpliceNodes()[static_cast<std::size_t>(lambda)].nextNode;
        if (!ValidNode(document, formals))
            break;
        auto tail = document.SpliceNodes()[static_cast<std::size_t>(formals)].nextNode;
        std::unordered_set<std::int32_t> bodyVisited;
        while (ValidNode(document, tail) && bodyVisited.insert(tail).second)
        {
            const auto next = document.SpliceNodes()[static_cast<std::size_t>(tail)].nextNode;
            if (next == -1)
                break;
            tail = next;
        }
        if (!ValidNode(document, tail) || document.SpliceNodes()[static_cast<std::size_t>(tail)].nextNode != -1)
        {
            result.message = "Repair the callback's invalid or cyclic body links before changing its result.";
            return result;
        }
        if (document.SpliceNodes()[static_cast<std::size_t>(tail)].tag == kBool)
        {
            auto &last = document.EditableSpliceNodes()[static_cast<std::size_t>(tail)];
            last.intValue = value ? 1 : 0;
            last.modified = true;
        }
        else
        {
            if (!CanAppendSpliceNodes(document, 1u))
            {
                result.message = "Changing the callback requires one available expression node.";
                return result;
            }
            const auto literal = AppendNode(document, kBool);
            auto &nodes = document.EditableSpliceNodes();
            nodes[static_cast<std::size_t>(literal)].intValue = value ? 1 : 0;
            nodes[static_cast<std::size_t>(tail)].nextNode = literal;
            nodes[static_cast<std::size_t>(tail)].modified = true;
        }
        module.modified = true;
        result.succeeded = true;
        result.moduleIndex = moduleIndex;
        result.message =
            std::string("Set ") + SpliceEventName(eventId) + " callback result to " +
            (value ? "true" : "false") + ", preserving its existing actions.";
        return result;
    }
    result.message =
        "Only a directly defined runnable callback can be edited; make the module unique or override an imported event first.";
    return result;
}

static SpliceAuthoringResult PrependSpliceSetSelfLiteral(BrxDocument &document, const std::int32_t objectIndex,
                                                         const std::uint32_t eventId, const std::int16_t eopid,
                                                         const std::uint8_t literalTag, const std::int32_t integerValue,
                                                         const float floatValue)
{
    SpliceAuthoringResult result;
    if (literalTag != kBool && literalTag != kS32 && literalTag != kF32)
    {
        result.message = "The requested friendly literal type is unsupported.";
        return result;
    }
    if ((literalTag == kF32 && !std::isfinite(floatValue)) || !CanAppendSpliceNodes(document, 5u))
    {
        result.message = "The action needs a finite value and five available expression nodes.";
        return result;
    }
    if (!ValidObject(document, objectIndex))
    {
        result.message = "Select a valid object first.";
        return result;
    }
    const auto &object = document.Objects()[static_cast<std::size_t>(objectIndex)];
    if (object.cid < 0 || static_cast<std::size_t>(object.cid) >= CidOptionIds().size() ||
        std::find(CidOptionIds()[static_cast<std::size_t>(object.cid)].begin(),
                  CidOptionIds()[static_cast<std::size_t>(object.cid)].end(),
                  eopid) == CidOptionIds()[static_cast<std::size_t>(object.cid)].end())
    {
        result.message = "EOPID " + std::to_string(eopid) + " is not legal for the selected CID.";
        return result;
    }
    const auto definition = EditorOptionDefinitionForCid(object.cid, eopid);
    const bool integerProperty =
        definition.kind == BrxPropertyKind::Int16 || definition.kind == BrxPropertyKind::TypedInt16 ||
        definition.kind == BrxPropertyKind::Int32 || definition.kind == BrxPropertyKind::UInt32;
    if ((definition.kind == BrxPropertyKind::Bool && literalTag != kBool) ||
        (definition.kind == BrxPropertyKind::Float && literalTag != kF32) || (integerProperty && literalTag != kS32) ||
        (!integerProperty && definition.kind != BrxPropertyKind::Bool && definition.kind != BrxPropertyKind::Float))
    {
        result.message = "The friendly scalar action does not support " +
                         std::string(PropertyKindName(definition.kind)) + " EOPID " + std::to_string(eopid) + ".";
        return result;
    }
    const auto moduleIndex = object.spliceIndex;
    if (moduleIndex < 0 || static_cast<std::size_t>(moduleIndex) >= document.SpliceModules().size())
    {
        result.message = "The object has no valid splice module.";
        return result;
    }
    auto &module = document.EditableSpliceModules()[static_cast<std::size_t>(moduleIndex)];
    const auto effectiveEvent = LatestDirectEvent(document, module, eventId);
    for (const auto expression : SpliceInitializationExpressions(document.SpliceNodes(), module.rootNode))
    {
        if (expression != effectiveEvent)
            continue;
        const auto define = document.SpliceNodes()[static_cast<std::size_t>(expression)].childNode;
        const auto eventSymbol =
            ValidNode(document, define) ? document.SpliceNodes()[static_cast<std::size_t>(define)].nextNode : -1;
        const auto lambdaExpression = ValidNode(document, eventSymbol)
                                          ? document.SpliceNodes()[static_cast<std::size_t>(eventSymbol)].nextNode
                                          : -1;
        const auto lambda = ValidNode(document, lambdaExpression)
                                ? document.SpliceNodes()[static_cast<std::size_t>(lambdaExpression)].childNode
                                : -1;
        if (!IsRunnableEventLambda(document, lambdaExpression))
            break;
        const auto formals =
            ValidNode(document, lambda) ? document.SpliceNodes()[static_cast<std::size_t>(lambda)].nextNode : -1;
        if (!ValidNode(document, formals))
            break;
        const auto selfFormal = document.SpliceNodes()[static_cast<std::size_t>(formals)].childNode;
        if (!ValidNode(document, selfFormal) ||
            document.SpliceNodes()[static_cast<std::size_t>(selfFormal)].tag != kSymid)
            break;
        const auto selfSymbolId = document.SpliceNodes()[static_cast<std::size_t>(selfFormal)].intValue;
        const auto oldBody = document.SpliceNodes()[static_cast<std::size_t>(formals)].nextNode;
        const auto call = AppendNode(document, kPair);
        const auto seto = AppendNode(document, kBifk);
        const auto self = AppendNode(document, kSymid);
        const auto option = AppendNode(document, kS32);
        const auto literal = AppendNode(document, literalTag);
        auto &nodes = document.EditableSpliceNodes();
        nodes[static_cast<std::size_t>(call)].childNode = seto;
        nodes[static_cast<std::size_t>(call)].nextNode = oldBody;
        nodes[static_cast<std::size_t>(seto)].intValue = 101; // g_mpbifkbif: seto!
        nodes[static_cast<std::size_t>(seto)].nextNode = self;
        nodes[static_cast<std::size_t>(self)].intValue = selfSymbolId;
        nodes[static_cast<std::size_t>(self)].nextNode = option;
        nodes[static_cast<std::size_t>(option)].intValue = eopid;
        nodes[static_cast<std::size_t>(option)].nextNode = literal;
        if (literalTag == kF32)
            nodes[static_cast<std::size_t>(literal)].floatValue[0] = floatValue;
        else
            nodes[static_cast<std::size_t>(literal)].intValue = integerValue;
        nodes[static_cast<std::size_t>(formals)].nextNode = call;
        nodes[static_cast<std::size_t>(formals)].modified = true;
        module.modified = true;
        result.succeeded = true;
        result.moduleIndex = moduleIndex;
        result.message = "Added seto! for EOPID " + std::to_string(eopid) + " to " + SpliceEventName(eventId) + ".";
        return result;
    }
    result.message = "The selected event is imported or does not have a runnable direct lambda.";
    return result;
}

SpliceAuthoringResult PrependSpliceSetSelfBoolean(BrxDocument &document, const std::int32_t objectIndex,
                                                  const std::uint32_t eventId, const std::int16_t eopid,
                                                  const bool value)
{
    return PrependSpliceSetSelfLiteral(document, objectIndex, eventId, eopid, kBool, value ? 1 : 0, 0.0f);
}

SpliceAuthoringResult PrependSpliceSetSelfInteger(BrxDocument &document, const std::int32_t objectIndex,
                                                  const std::uint32_t eventId, const std::int16_t eopid,
                                                  const std::int32_t value)
{
    return PrependSpliceSetSelfLiteral(document, objectIndex, eventId, eopid, kS32, value, 0.0f);
}

SpliceAuthoringResult PrependSpliceSetSelfFloat(BrxDocument &document, const std::int32_t objectIndex,
                                                const std::uint32_t eventId, const std::int16_t eopid,
                                                const float value)
{
    return PrependSpliceSetSelfLiteral(document, objectIndex, eventId, eopid, kF32, 0, value);
}

SpliceAuthoringResult CreateAlternatingWarpTemplate(BrxDocument &document, const std::int32_t controllerObjectIndex,
                                                    const std::int32_t firstWarpObjectIndex,
                                                    const std::int32_t secondWarpObjectIndex)
{
    SpliceAuthoringResult result;
    if (!ValidObject(document, controllerObjectIndex))
    {
        result.message = "Select a valid controller object first.";
        return result;
    }
    if (!ValidObject(document, firstWarpObjectIndex) || !ValidObject(document, secondWarpObjectIndex))
    {
        result.message = "Choose two valid WARP destination objects.";
        return result;
    }
    const auto &firstWarp = document.Objects()[static_cast<std::size_t>(firstWarpObjectIndex)];
    const auto &secondWarp = document.Objects()[static_cast<std::size_t>(secondWarpObjectIndex)];
    if (!CidDerivesFrom(firstWarp.cid, 116) || !CidDerivesFrom(secondWarp.cid, 116))
    {
        result.message = "Both destinations must be WARP (CID 116) objects.";
        return result;
    }
    if (firstWarpObjectIndex == secondWarpObjectIndex || firstWarp.oid == secondWarp.oid)
    {
        result.message = "Alternating warp requires two different destination OIDs.";
        return result;
    }
    if (firstWarp.oid < 0 || secondWarp.oid < 0)
    {
        result.message = "Both WARP destinations need serialized, non-negative OIDs.";
        return result;
    }
    if (document.SpliceModules().size() >= 1'000'000)
    {
        result.message = "Creating the alternating-warp module would exceed the loader's splice limit.";
        return result;
    }

    const auto &controller = document.Objects()[static_cast<std::size_t>(controllerObjectIndex)];
    const auto previousModule = controller.spliceIndex;
    std::ostringstream source;
    source << "; Alternating WARP generated from ProjectCane semantics.\n"
           << "; Trigger [2] calls WARP Trigger EOPID 162; false starts at A.\n"
           << "(begin\n";
    if (previousModule >= 0 && static_cast<std::size_t>(previousModule) < document.SpliceModules().size())
        source << "  (import " << previousModule << ")\n";
    source << "  (define $alternatingWarpState false)\n"
           << "  (define sym#2\n"
           << "    (lambda ($self)\n"
           << "      (if $alternatingWarpState\n"
           << "          (begin\n"
           << "            (seto! (find-object " << secondWarp.oid << " tag#0:0) 162 0)\n"
           << "            (set! $alternatingWarpState false)\n"
           << "            true)\n"
           << "          (begin\n"
           << "            (seto! (find-object " << firstWarp.oid << " tag#0:0) 162 0)\n"
           << "            (set! $alternatingWarpState true)\n"
           << "            true))))\n"
           << ")\n";

    const auto validation =
        ValidateSpliceModuleSource(source.str(), controller.cid, document.WorldHeader().symbolCount, &document);
    if (!validation.succeeded)
    {
        result.message = validation.diagnostics.empty() ? "Generated alternating-warp source did not validate."
                                                        : "Generated alternating-warp source did not validate: " +
                                                              validation.diagnostics.front().message;
        return result;
    }
    const auto previousNodeCount = document.SpliceNodes().size();
    const auto created = CreateSpliceModule(document, controllerObjectIndex);
    if (!created.succeeded)
        return created;
    const auto applied = ApplySpliceModuleSource(document, created.moduleIndex, source.str(), controller.cid);
    if (!applied.succeeded)
    {
        auto &editedController = document.EditableObjects()[static_cast<std::size_t>(controllerObjectIndex)];
        editedController.spliceIndex = previousModule;
        editedController.spliceModified = true;
        document.EditableSpliceModules().pop_back();
        document.EditableSpliceNodes().resize(previousNodeCount);
        result.message = applied.diagnostics.empty()
                             ? "Could not apply the alternating-warp source."
                             : "Could not apply the alternating-warp source: " + applied.diagnostics.front().message;
        return result;
    }
    result.succeeded = true;
    result.moduleIndex = created.moduleIndex;
    result.message = "Created Trigger logic alternating WARP " + std::to_string(firstWarp.oid) + " and WARP " +
                     std::to_string(secondWarp.oid) + ". The previous module remains imported.";
    return result;
}

void CompactSpliceNodes(BrxDocument &document)
{
    const auto &source = document.SpliceNodes();
    std::vector<std::uint8_t> reachable(source.size(), 0);
    std::vector<std::int32_t> pending;
    pending.reserve(document.SpliceModules().size());
    for (const auto &module : document.SpliceModules())
        pending.push_back(module.rootNode);
    while (!pending.empty())
    {
        const auto index = pending.back();
        pending.pop_back();
        if (index < 0 || static_cast<std::size_t>(index) >= source.size() || reachable[static_cast<std::size_t>(index)])
            continue;
        reachable[static_cast<std::size_t>(index)] = 1;
        pending.push_back(source[static_cast<std::size_t>(index)].childNode);
        pending.push_back(source[static_cast<std::size_t>(index)].nextNode);
    }
    std::vector<std::int32_t> remap(source.size(), -1);
    std::vector<BrxSpliceNode> compacted;
    compacted.reserve(static_cast<std::size_t>(std::count(reachable.begin(), reachable.end(), 1)));
    for (std::size_t index = 0; index < source.size(); ++index)
    {
        if (!reachable[index])
            continue;
        remap[index] = static_cast<std::int32_t>(compacted.size());
        compacted.push_back(source[index]);
    }
    const auto mapped = [&](const std::int32_t index)
    {
        // Compaction must not silently repair malformed links in another
        // module. Preserve invalid identities for the validator/writer to flag.
        return index >= 0 && static_cast<std::size_t>(index) < remap.size()
            ? remap[static_cast<std::size_t>(index)] : index;
    };
    for (auto &node : compacted)
    {
        node.childNode = mapped(node.childNode);
        node.nextNode = mapped(node.nextNode);
    }
    for (auto &module : document.EditableSpliceModules())
    {
        module.rootNode = mapped(module.rootNode);
        module.modified = true;
    }
    document.EditableSpliceNodes() = std::move(compacted);
}
