#include "sqc_parser.h"

#include "tokenizer.h"
#include "parser.tab.hh"

#include "../opcodes/common.h"
#include "../runtime/d_array.h"
#include "../runtime/d_string.h"
#include "../runtime/d_boolean.h"
#include "../runtime/d_code.h"
#include <algorithm>
#include <charconv>

using namespace std::string_literals;
using namespace std::string_view_literals;

namespace sqf::sqc::util
{
    class setbuilder {
    private:
        std::vector<::sqf::runtime::instruction::sptr> inner;
        std::string_view m_contents;
    public:
        setbuilder(std::string_view contents) : m_contents(contents) {}

        setbuilder create_from() const
        {
            return { m_contents };
        }
        void push_back(const ::sqf::sqc::tokenizer::token& t, ::sqf::runtime::instruction::sptr ptr)
        {
            ptr->diag_info({ t.line, t.column, t.offset, { t.path, {} }, ::sqf::runtime::parser::sqf::create_code_segment(m_contents, t.offset, t.contents.length()) });
            inner.push_back(ptr);
        }
        operator ::sqf::runtime::instruction_set() const
        {
            return { inner };
        }
    };
}

void sqf::sqc::parser::to_assembly(::sqf::runtime::runtime& runtime, util::setbuilder& set, std::vector<std::string>& locals, ::sqf::sqc::bison::astnode& node)
{
    switch (node.kind)
    {
    case ::sqf::sqc::bison::astkind::RETURN: {
        if (node.children.empty())
        {
            set.push_back(node.token, std::make_shared<opcodes::push>(__scopename_function));
            set.push_back(node.token, std::make_shared<opcodes::call_unary>("breakout"s));
        }
        else
        {
            to_assembly(runtime, set, locals, node.children[0]);
            set.push_back(node.token, std::make_shared<opcodes::push>(__scopename_function));
            set.push_back(node.token, std::make_shared<opcodes::call_binary>("breakout"s, (short)4));
        }
    } break;
    case ::sqf::sqc::bison::astkind::THROW: {
        to_assembly(runtime, set, locals, node.children[0]);
        set.push_back(node.token, std::make_shared<opcodes::call_unary>("throw"s));
    } break;
    case ::sqf::sqc::bison::astkind::ASSIGNMENT: {
        to_assembly(runtime, set, locals, node.children[1]);
        std::string var(node.children[0].token.contents);
        if (std::find(locals.begin(), locals.end(), var) != locals.end())
        {
            set.push_back(node.children[0].token, std::make_shared<opcodes::assign_to>("_" + var));
        }
        else
        {
            set.push_back(node.children[0].token, std::make_shared<opcodes::assign_to>(var));
        }
    } break;
    case ::sqf::sqc::bison::astkind::OP_ARRAY_SET: {
        // Push actual array onto value stack
        to_assembly(runtime, set, locals, node.children[0]);

        // Push Index-Expression to stack
        to_assembly(runtime, set, locals, node.children[1]);

        // Push Value-Expression to stack
        to_assembly(runtime, set, locals, node.children[2]);

        // Emit "makeArray" instruction to craft the right-handed argument
        set.push_back(node.token, std::make_shared<opcodes::make_array>(2));

        // Emit "set" to perform the array assignment
        set.push_back(node.token, std::make_shared<opcodes::call_binary>("set"s, (short)4));
    } break;
    case ::sqf::sqc::bison::astkind::OP_ARRAY_GET: {
        // Push actual array onto value stack
        to_assembly(runtime, set, locals, node.children[0]);

        // Push Index-Expression to stack
        to_assembly(runtime, set, locals, node.children[1]);

        // Emit "select" to perform the array index access
        set.push_back(node.token, std::make_shared<opcodes::call_binary>("select"s, (short)4));
    } break;
    case ::sqf::sqc::bison::astkind::DECLARATION: {
        to_assembly(runtime, set, locals, node.children[1]);
        std::string var(node.children[0].token.contents);
        locals.push_back(var);
        set.push_back(node.children[0].token, std::make_shared<opcodes::assign_to_local>("_"s + var));
    } break;
    case ::sqf::sqc::bison::astkind::FORWARD_DECLARATION: {
        set.push_back(node.children[0].token, std::make_shared<opcodes::push>(node.children[0].token.contents));
        set.push_back(node.token, std::make_shared<opcodes::call_unary>("private"s));
    } break;
    case ::sqf::sqc::bison::astkind::FUNCTION_DECLARATION: {
        auto local_set = set.create_from();
        std::vector<std::string> new_locals;
        local_set.push_back(node.token, std::make_shared<opcodes::push>(__scopename_function));
        local_set.push_back(node.token, std::make_shared<opcodes::call_unary>("scopename"));

        to_assembly(runtime, local_set, new_locals, node.children[1]);
        to_assembly(runtime, local_set, new_locals, node.children[2]);
        set.push_back(node.children[0].token, std::make_shared<opcodes::push>(runtime::instruction_set{ local_set }));
        set.push_back(node.token, std::make_shared<opcodes::assign_to>(std::string(node.children[0].token.contents)));
    } break;
    case ::sqf::sqc::bison::astkind::FUNCTION: {
        auto local_set = set.create_from();
        std::vector<std::string> new_locals;
        local_set.push_back(node.token, std::make_shared<opcodes::push>(__scopename_function));
        local_set.push_back(node.token, std::make_shared<opcodes::call_unary>("scopename"));

        to_assembly(runtime, local_set, new_locals, node.children[0]);
        to_assembly(runtime, local_set, new_locals, node.children[1]);
        set.push_back(node.token, std::make_shared<opcodes::push>(runtime::instruction_set{ local_set }));
    } break;
    case ::sqf::sqc::bison::astkind::ARGLIST: {
        if (!node.children.empty())
        {
            for (auto child : node.children)
            {
                auto var = std::string(child.token.contents);
                auto lvar = "_"s + var;
                locals.push_back(var);
                set.push_back(child.token, std::make_shared<opcodes::push>(lvar));
            }
            set.push_back(node.token, std::make_shared<opcodes::make_array>(node.children.size()));
            set.push_back(node.token, std::make_shared<opcodes::call_unary>("params"s));
        }
    } break;
    case ::sqf::sqc::bison::astkind::CODEBLOCK: {
        // Create copy of locals to ensure
        // locals stay local to this scope.
        auto locals_copy = locals;
        for (auto child : node.children)
        {
            to_assembly(runtime, set, locals_copy, child);
        }
    } break;
    case ::sqf::sqc::bison::astkind::IF: {
        // Emit condition
        to_assembly(runtime, set, locals, node.children[0]);
        set.push_back(node.children[0].token, std::make_shared<opcodes::call_unary>("if"s));

        auto local_set = set.create_from();
        to_assembly(runtime, local_set, locals, node.children[1]);
        set.push_back(node.children[1].token, std::make_shared<opcodes::push>(runtime::instruction_set{ local_set }));

        set.push_back(node.token, std::make_shared<opcodes::call_binary>("then"s, (short)4));
    } break;
    case ::sqf::sqc::bison::astkind::OP_TERNARY: /* fallthrough */
    case ::sqf::sqc::bison::astkind::IFELSE: {
        // Emit condition
        to_assembly(runtime, set, locals, node.children[0]);
        set.push_back(node.children[0].token, std::make_shared<opcodes::call_unary>("if"s));

        // Emit on-true
        auto local_set1 = set.create_from();
        to_assembly(runtime, local_set1, locals, node.children[1]);
        set.push_back(node.children[1].token, std::make_shared<opcodes::push>(runtime::instruction_set{ local_set1 }));

        // Emit on-false
        auto local_set2 = set.create_from();
        to_assembly(runtime, local_set2, locals, node.children[2]);
        set.push_back(node.children[2].token, std::make_shared<opcodes::push>(runtime::instruction_set{ local_set2 }));
        set.push_back(node.children[2].token, std::make_shared<opcodes::call_binary>("else"s, (short)5));

        set.push_back(node.token, std::make_shared<opcodes::call_binary>("then"s, (short)4));
    } break;
    case ::sqf::sqc::bison::astkind::FOR: {
        // Emit "for"
        std::string for_var(node.children[0].token.contents);
        std::string for_lvar = "_"s + for_var;
        set.push_back(node.children[0].token, std::make_shared<opcodes::push>(for_lvar));
        set.push_back(node.children[0].token, std::make_shared<opcodes::call_unary>("for"s));

        // Emit "from"
        to_assembly(runtime, set, locals, node.children[1]);
        set.push_back(node.children[1].token, std::make_shared<opcodes::call_binary>("from"s, (short)4));

        // Emit "to"
        to_assembly(runtime, set, locals, node.children[2]);
        set.push_back(node.children[2].token, std::make_shared<opcodes::call_binary>("to"s, (short)4));

        // Emit Codeblock
        {
            // Create additional instruction_set vector
            auto local_set1 = set.create_from();

            // Create copy of locals where for_var exists
            auto locals_copy = locals;
            locals_copy.push_back(for_var);

            // Fill actual instruction_set
            to_assembly(runtime, local_set1, locals_copy, node.children[3]);
            set.push_back(node.children[3].token, std::make_shared<opcodes::push>(runtime::instruction_set{ local_set1 }));
        }

        // Emit "do"
        set.push_back(node.token, std::make_shared<opcodes::call_binary>("do"s, (short)4));
    } break;
    case ::sqf::sqc::bison::astkind::FORSTEP: {
        // Emit "for"
        std::string for_var(node.children[0].token.contents);
        std::string for_lvar = "_"s + for_var;
        set.push_back(node.children[0].token, std::make_shared<opcodes::push>(for_lvar));
        set.push_back(node.children[0].token, std::make_shared<opcodes::call_unary>("for"s));

        // Emit "from"
        to_assembly(runtime, set, locals, node.children[1]);
        set.push_back(node.children[1].token, std::make_shared<opcodes::call_binary>("from"s, (short)4));

        // Emit "to"
        to_assembly(runtime, set, locals, node.children[2]);
        set.push_back(node.children[2].token, std::make_shared<opcodes::call_binary>("to"s, (short)4));

        // Emit "step"
        to_assembly(runtime, set, locals, node.children[3]);
        set.push_back(node.children[3].token, std::make_shared<opcodes::call_binary>("step"s, (short)4));

        // Emit Codeblock
        {
            // Create additional instruction_set vector
            auto local_set1 = set.create_from();

            // Create copy of locals where for_var exists
            auto locals_copy = locals;
            locals_copy.push_back(for_var);

            // Fill actual instruction_set
            to_assembly(runtime, local_set1, locals_copy, node.children[4]);
            set.push_back(node.children[4].token, std::make_shared<opcodes::push>(runtime::instruction_set{ local_set1 }));
        }

        // Emit "do"
        set.push_back(node.token, std::make_shared<opcodes::call_binary>("do"s, (short)4));
    } break;
    case ::sqf::sqc::bison::astkind::FOREACH: {

        // Emit Codeblock
        {
            // Create additional instruction_set vector
            auto local_set1 = set.create_from();

            // Assign variable for forEach _x
            set.push_back(node.children[0].token, std::make_shared<opcodes::get_variable>("_x"s));
            std::string var(node.children[0].token.contents);
            std::string lvar = "_"s + var;
            set.push_back(node.children[0].token, std::make_shared<opcodes::assign_to_local>(lvar));

            // Create copy of locals where `_x` exists
            auto locals_copy = locals;
            locals_copy.push_back(var);

            // Fill actual instruction_set
            to_assembly(runtime, local_set1, locals_copy, node.children[2]);
            set.push_back(node.children[2].token, std::make_shared<opcodes::push>(runtime::instruction_set{ local_set1 }));
        }
        // Emit value
        to_assembly(runtime, set, locals, node.children[1]);

        // Emit "forEach"
        set.push_back(node.token, std::make_shared<opcodes::call_binary>("foreach"s, (short)4));
    } break;
    case ::sqf::sqc::bison::astkind::WHILE: {

        // Emit "while"
        {
            // Create additional instruction_set vector for condition expression
            auto local_set1 = set.create_from();

            // Fill actual instruction_set for code
            to_assembly(runtime, local_set1, locals, node.children[0]);
            set.push_back(node.children[0].token, std::make_shared<opcodes::push>(runtime::instruction_set{ local_set1 }));
        }
        set.push_back(node.token, std::make_shared<opcodes::call_binary>("while"s, (short)4));

        // Emit codeblock
        {
            // Create additional instruction_set vector for condition expression
            auto local_set1 = set.create_from();

            // Fill actual instruction_set for code
            to_assembly(runtime, local_set1, locals, node.children[1]);
            set.push_back(node.children[1].token, std::make_shared<opcodes::push>(runtime::instruction_set{ local_set1 }));
        }

        // Emit "do"
        set.push_back(node.token, std::make_shared<opcodes::call_binary>("do"s, (short)4));
    } break;
    case ::sqf::sqc::bison::astkind::DOWHILE: {

        // Emit call before while for the "do once"
        {
            // Create additional instruction_set vector for condition expression
            auto local_set1 = set.create_from();

            // Fill actual instruction_set for code
            to_assembly(runtime, local_set1, locals, node.children[0]);
            set.push_back(node.children[0].token, std::make_shared<opcodes::push>(runtime::instruction_set{ local_set1 }));

            // Use unary call to call created scope
            set.push_back(node.children[0].token, std::make_shared<opcodes::call_unary>("call"s));
        }

        // Emit "while"
        {
            // Create additional instruction_set vector for condition expression
            auto local_set1 = set.create_from();

            // Fill actual instruction_set for code
            to_assembly(runtime, local_set1, locals, node.children[1]);
            set.push_back(node.children[1].token, std::make_shared<opcodes::push>(runtime::instruction_set{ local_set1 }));
        }
        set.push_back(node.children[1].token, std::make_shared<opcodes::call_binary>("while"s, (short)4));

        // Emit codeblock
        {
            // Create additional instruction_set vector for condition expression
            auto local_set1 = set.create_from();

            // Fill actual instruction_set for code
            to_assembly(runtime, local_set1, locals, node.children[0]);
            set.push_back(node.children[0].token, std::make_shared<opcodes::push>(runtime::instruction_set{ local_set1 }));
        }

        // Emit "do"
        set.push_back(node.token, std::make_shared<opcodes::call_binary>("do"s, (short)4));
    } break;
    case ::sqf::sqc::bison::astkind::TRYCATCH: {
        // Emit "try"
        {
            // Create additional instruction_set vector for condition expression
            auto local_set1 = set.create_from();

            // Fill actual instruction_set for code
            to_assembly(runtime, local_set1, locals, node.children[0]);
            set.push_back(node.children[0].token, std::make_shared<opcodes::push>(runtime::instruction_set{ local_set1 }));

            // Use unary call to call created scope
            set.push_back(node.children[0].token, std::make_shared<opcodes::call_unary>("try"s));
        }

        // Emit "catch"
        {
            // Create additional instruction_set vector
            auto local_set1 = set.create_from();

            // Assign variable for _exception
            set.push_back(node.children[0].token, std::make_shared<opcodes::get_variable>("_exception"s));
            std::string var(node.children[0].token.contents);
            std::string lvar = "_"s + var;
            set.push_back(node.children[0].token, std::make_shared<opcodes::assign_to_local>(lvar));

            // Create copy of locals where `_x` exists
            auto locals_copy = locals;
            locals_copy.push_back(var);

            // Fill actual instruction_set for code
            to_assembly(runtime, local_set1, locals, node.children[1]);
            set.push_back(node.children[1].token, std::make_shared<opcodes::push>(runtime::instruction_set{ local_set1 }));
        }
        set.push_back(node.children[1].token, std::make_shared<opcodes::call_binary>("catch"s, (short)4));
    } break;
    case ::sqf::sqc::bison::astkind::SWITCH: {
        // Emit switch
        to_assembly(runtime, set, locals, node.children[0]);
        set.push_back(node.children[0].token, std::make_shared<opcodes::call_unary>("switch"s));

        // Emit "do switch"
        {
            // Create additional instruction_set vector for condition expression
            auto local_set1 = set.create_from();

            // Fill actual instruction_set with cases
            for (auto it = node.children.begin() + 1; it != node.children.end(); it++)
            {
                to_assembly(runtime, local_set1, locals, *it);
            }
            set.push_back(node.token, std::make_shared<opcodes::push>(runtime::instruction_set{ local_set1 }));

            // Emit "do"
            set.push_back(node.token, std::make_shared<opcodes::call_binary>("do"s, (short)4));
        }
    } break;
    case ::sqf::sqc::bison::astkind::CASE: {
        // Emit "case"
        to_assembly(runtime, set, locals, node.children[0]);
        set.push_back(node.children[0].token, std::make_shared<opcodes::call_unary>("case"s));

        // If has codeblock, emit ":"
        if (node.children.size() == 2) {
            // Create additional instruction_set vector for condition expression
            auto local_set1 = set.create_from();

            // Fill actual instruction_set for code
            to_assembly(runtime, local_set1, locals, node.children[1]);
            set.push_back(node.children[1].token, std::make_shared<opcodes::push>(runtime::instruction_set{ local_set1 }));

            // Emit "colon"
            set.push_back(node.token, std::make_shared<opcodes::call_binary>(":"s, (short)4));
        }
    } break;
    case ::sqf::sqc::bison::astkind::CASE_DEFAULT: {
        // Create additional instruction_set vector for condition expression
        auto local_set1 = set.create_from();

        // Fill actual instruction_set for code
        to_assembly(runtime, local_set1, locals, node.children[0]);
        set.push_back(node.children[0].token, std::make_shared<opcodes::push>(runtime::instruction_set{ local_set1 }));

        // Emit "default"
        set.push_back(node.token, std::make_shared<opcodes::call_unary>("default"s));
    } break;
    case ::sqf::sqc::bison::astkind::OP_OR: {
        // Emit Left-Argument
        to_assembly(runtime, set, locals, node.children[0]);
        // Emit Right-Argument
        to_assembly(runtime, set, locals, node.children[1]);

        // Emit "or"
        set.push_back(node.token, std::make_shared<opcodes::call_binary>("||"s, (short)1));
    } break;
    case ::sqf::sqc::bison::astkind::OP_AND: {
        // Emit Left-Argument
        to_assembly(runtime, set, locals, node.children[0]);
        // Emit Right-Argument
        to_assembly(runtime, set, locals, node.children[1]);

        // Emit "and"
        set.push_back(node.token, std::make_shared<opcodes::call_binary>("&&"s, (short)2));
    } break;
    case ::sqf::sqc::bison::astkind::OP_EQUALEXACT: {
        // Emit Left-Argument
        to_assembly(runtime, set, locals, node.children[0]);
        // Emit Right-Argument
        to_assembly(runtime, set, locals, node.children[1]);

        // Emit "==="
        set.push_back(node.token, std::make_shared<opcodes::call_binary>("isequalto"s, (short)4));
    } break;
    case ::sqf::sqc::bison::astkind::OP_EQUAL: {
        // Emit Left-Argument
        to_assembly(runtime, set, locals, node.children[0]);
        // Emit Right-Argument
        to_assembly(runtime, set, locals, node.children[1]);

        // Emit "=="
        set.push_back(node.token, std::make_shared<opcodes::call_binary>("=="s, (short)3));
    } break;
    case ::sqf::sqc::bison::astkind::OP_NOTEQUALEXACT: {
        // Emit Left-Argument
        to_assembly(runtime, set, locals, node.children[0]);
        // Emit Right-Argument
        to_assembly(runtime, set, locals, node.children[1]);

        // Emit "!=="
        set.push_back(node.token, std::make_shared<opcodes::call_binary>("isequalto"s, (short)4));
        set.push_back(node.token, std::make_shared<opcodes::call_unary>("!"s));
    } break;
    case ::sqf::sqc::bison::astkind::OP_NOTEQUAL: {
        // Emit Left-Argument
        to_assembly(runtime, set, locals, node.children[0]);
        // Emit Right-Argument
        to_assembly(runtime, set, locals, node.children[1]);

        // Emit "!="
        set.push_back(node.token, std::make_shared<opcodes::call_binary>("!="s, (short)3));
    } break;
    case ::sqf::sqc::bison::astkind::OP_LESSTHAN: {
        // Emit Left-Argument
        to_assembly(runtime, set, locals, node.children[0]);
        // Emit Right-Argument
        to_assembly(runtime, set, locals, node.children[1]);

        // Emit "<"
        set.push_back(node.token, std::make_shared<opcodes::call_binary>("<"s, (short)3));
    } break;
    case ::sqf::sqc::bison::astkind::OP_GREATERTHAN: {
        // Emit Left-Argument
        to_assembly(runtime, set, locals, node.children[0]);
        // Emit Right-Argument
        to_assembly(runtime, set, locals, node.children[1]);

        // Emit ">"
        set.push_back(node.token, std::make_shared<opcodes::call_binary>(">"s, (short)3));
    } break;
    case ::sqf::sqc::bison::astkind::OP_LESSTHANEQUAL: {
        // Emit Left-Argument
        to_assembly(runtime, set, locals, node.children[0]);
        // Emit Right-Argument
        to_assembly(runtime, set, locals, node.children[1]);

        // Emit "<="
        set.push_back(node.token, std::make_shared<opcodes::call_binary>("<="s, (short)3));
    } break;
    case ::sqf::sqc::bison::astkind::OP_GREATERTHANEQUAL: {
        // Emit Left-Argument
        to_assembly(runtime, set, locals, node.children[0]);
        // Emit Right-Argument
        to_assembly(runtime, set, locals, node.children[1]);

        // Emit ">="
        set.push_back(node.token, std::make_shared<opcodes::call_binary>(">="s, (short)3));
    } break;
    case ::sqf::sqc::bison::astkind::OP_PLUS: {
        // Emit Left-Argument
        to_assembly(runtime, set, locals, node.children[0]);
        // Emit Right-Argument
        to_assembly(runtime, set, locals, node.children[1]);

        // Emit "+"
        set.push_back(node.token, std::make_shared<opcodes::call_binary>("+"s, (short)6));
    } break;
    case ::sqf::sqc::bison::astkind::OP_MINUS: {
        // Emit Left-Argument
        to_assembly(runtime, set, locals, node.children[0]);
        // Emit Right-Argument
        to_assembly(runtime, set, locals, node.children[1]);

        // Emit "-"
        set.push_back(node.token, std::make_shared<opcodes::call_binary>("-"s, (short)6));
    } break;
    case ::sqf::sqc::bison::astkind::OP_MULTIPLY: {
        // Emit Left-Argument
        to_assembly(runtime, set, locals, node.children[0]);
        // Emit Right-Argument
        to_assembly(runtime, set, locals, node.children[1]);

        // Emit "*"
        set.push_back(node.token, std::make_shared<opcodes::call_binary>("*"s, (short)7));
    } break;
    case ::sqf::sqc::bison::astkind::OP_DIVIDE: {
        // Emit Left-Argument
        to_assembly(runtime, set, locals, node.children[0]);
        // Emit Right-Argument
        to_assembly(runtime, set, locals, node.children[1]);

        // Emit "/"
        set.push_back(node.token, std::make_shared<opcodes::call_binary>("/"s, (short)7));
    } break;
    case ::sqf::sqc::bison::astkind::OP_REMAINDER: {
        // Emit Left-Argument
        to_assembly(runtime, set, locals, node.children[0]);
        // Emit Right-Argument
        to_assembly(runtime, set, locals, node.children[1]);

        // Emit "%"
        set.push_back(node.token, std::make_shared<opcodes::call_binary>("%"s, (short)7));
    } break;
    case ::sqf::sqc::bison::astkind::OP_NOT: {
        // Emit Left-Argument
        to_assembly(runtime, set, locals, node.children[0]);

        // Emit "!"
        set.push_back(node.token, std::make_shared<opcodes::call_unary>("!"s));
    } break;
    case ::sqf::sqc::bison::astkind::OP_BINARY: {
        auto opname = std::string(node.children[1].token.contents);
        std::transform(opname.begin(), opname.end(), opname.begin(), [](char c) { return (char)std::tolower(c); });
        if (runtime.sqfop_exists_binary(opname))
        {
            // Emit Left-Argument
            to_assembly(runtime, set, locals, node.children[0]);

            auto matchingOps = runtime.sqfop_binary_by_name(opname);

            if (node.children[2].children.size() > 1 || matchingOps.front().get().right_type() == runtime::t_array())
            {
                // Emit Right-Argument
                to_assembly(runtime, set, locals, node.children[2]);
                set.push_back(node.children[2].token, std::make_shared<opcodes::make_array>(std::max(node.children[2].children.size(), (size_t)1)));
            }
            else
            {
                // Emit Right-Argument
                to_assembly(runtime, set, locals, node.children[2]);
            }

            // Emit binary operator
            set.push_back(node.token, std::make_shared<opcodes::call_binary>(std::string(matchingOps.front().get().name()), matchingOps.front().get().precedence()));
        }
        else
        {
            log(logmessage::runtime::ErrorMessage({}, "SQC", "unknown operator: " + opname));
            set.push_back(node.token, std::make_shared<opcodes::call_nular>("nil"s));
        }
    } break;
    case ::sqf::sqc::bison::astkind::OP_UNARY: {
        auto opname = std::string(node.children[0].token.contents);
        std::transform(opname.begin(), opname.end(), opname.begin(), [](char c) { return std::tolower(c); });
        if (runtime.sqfop_exists_unary(opname))
        {
            auto matchingOps = runtime.sqfop_unary_by_name(opname);

            if (node.children[1].children.size() > 1 || matchingOps.front().get().right_type() == runtime::t_array())
            {
                // Emit Right-Argument
                to_assembly(runtime, set, locals, node.children[1]);
                set.push_back(node.children[1].token, std::make_shared<opcodes::make_array>(std::max(node.children[1].children.size(), (size_t)1)));
            }
            else
            {
                // Emit Right-Argument
                to_assembly(runtime, set, locals, node.children[1]);
            }

            // Emit binary operator
            set.push_back(node.token, std::make_shared<opcodes::call_unary>(std::string(matchingOps.front().get().name())));
        }
        else
        {
            // Emit Right-Argument
            to_assembly(runtime, set, locals, node.children[1]);
            set.push_back(node.token, std::make_shared<opcodes::make_array>(std::max(node.children[1].children.size(), (size_t)1)));

            // Emit function
            std::string var(node.children[0].token.contents);
            if (std::find(locals.begin(), locals.end(), var) != locals.end())
            {
                set.push_back(node.children[0].token, std::make_shared<opcodes::get_variable>("_" + var));
            }
            else
            {
                set.push_back(node.children[0].token, std::make_shared<opcodes::get_variable>(var));
            }

            // Emit binary operator
            set.push_back(node.token, std::make_shared<opcodes::call_binary>("call"s, (short)4));
        }
    } break;
    case ::sqf::sqc::bison::astkind::VAL_STRING: {
        set.push_back(node.token, std::make_shared<opcodes::push>(types::d_string::from_sqf(node.token.contents)));
    } break;
    case ::sqf::sqc::bison::astkind::VAL_ARRAY: {
        for (auto child : node.children)
        {
            to_assembly(runtime, set, locals, child);
        }
        set.push_back(node.token, std::make_shared<opcodes::make_array>(node.children.size()));
    } break;
    case ::sqf::sqc::bison::astkind::VAL_NUMBER: {
        double d;
        try
        {
            set.push_back(node.token, std::make_shared<::sqf::opcodes::push>(::sqf::runtime::value(std::make_shared<::sqf::types::d_scalar>((double)std::stod(std::string(node.token.contents))))));
        }
        catch (std::out_of_range&)
        {
            log(logmessage::assembly::NumberOutOfRange({}));
            set.push_back(node.token, std::make_shared<::sqf::opcodes::push>(::sqf::runtime::value(std::make_shared<::sqf::types::d_scalar>(std::nanf("")))));
        }
        // We cannot use "modern" variant due to lack of GCC support in GitHub Actions as of 29.08.2020
        // auto result = std::from_chars(node.token.contents.data(), node.token.contents.data() + node.token.contents.size(), d);
        // if (result.ec == std::errc())
        // {
        //     log(logmessage::runtime::ReturningScalarZero({}));
        //     set.push_back(std::make_shared<::sqf::opcodes::push>(std::make_shared<::sqf::types::d_scalar>(0)));
        // }
        // else
        // {
        //     set.push_back(std::make_shared<::sqf::opcodes::push>(std::make_shared<::sqf::types::d_scalar>(d)));
        // }
    } break;
    case ::sqf::sqc::bison::astkind::VAL_TRUE: {
        set.push_back(node.token, std::make_shared<::sqf::opcodes::push>(true));
    } break;
    case ::sqf::sqc::bison::astkind::VAL_FALSE: {
        set.push_back(node.token, std::make_shared<::sqf::opcodes::push>(false));
    } break;
    case ::sqf::sqc::bison::astkind::VAL_NIL: {
        set.push_back(node.token, std::make_shared<::sqf::opcodes::push>(runtime::value{}));
    } break;
    case ::sqf::sqc::bison::astkind::GET_VARIABLE: {
        std::string var(node.token.contents);
        if (std::find(locals.begin(), locals.end(), var) != locals.end())
        {
            set.push_back(node.token, std::make_shared<opcodes::get_variable>("_" + var));
        }
        else
        {
            set.push_back(node.token, std::make_shared<opcodes::get_variable>(var));
        }
    } break;
    default:
        for (auto child : node.children)
        {
            to_assembly(runtime, set, locals, child);
        }
        break;
    }
}

bool sqf::sqc::parser::check_syntax(::sqf::runtime::runtime& runtime, std::string contents, ::sqf::runtime::fileio::pathinfo file)
{
    tokenizer t(contents.begin(), contents.end(), file.physical);
    ::sqf::sqc::bison::astnode res;
    ::sqf::sqc::bison::parser p(t, res, *this, file.physical);
    bool success = p.parse() == 0;
    return success;
}
std::optional<::sqf::runtime::instruction_set> sqf::sqc::parser::parse(::sqf::runtime::runtime& runtime, std::string contents, ::sqf::runtime::fileio::pathinfo file)
{
    tokenizer t(contents.begin(), contents.end(), file.physical);
    util::setbuilder source(contents);
    ::sqf::sqc::bison::astnode res;
    ::sqf::sqc::bison::parser p(t, res, *this, file.physical);
    // p.set_debug_level(1);
    bool success = p.parse() == 0;
    if (!success)
    {
        return {};
    }
    std::vector<std::string> locals;
    to_assembly(runtime, source, locals, res);
    return source;
}