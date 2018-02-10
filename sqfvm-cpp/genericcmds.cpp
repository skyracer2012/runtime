#include "full.h"

using namespace sqf;
namespace
{
	value_s call_code(const virtualmachine* vm, value_s right)
	{
		auto r = std::static_pointer_cast<codedata>(right->data());
		r->loadinto(vm->stack());
		vm->stack()->stacks_top()->setvar(L"_this", std::make_shared<value>());
		return value_s();
	}
	value_s call_any_code(const virtualmachine* vm, value_s left, value_s right)
	{
		auto r = std::static_pointer_cast<codedata>(right->data());
		r->loadinto(vm->stack());
		vm->stack()->stacks_top()->setvar(L"_this", std::make_shared<value>());
		vm->stack()->pushval(left);
		return value_s();
	}
	value_s count_array(const virtualmachine* vm, value_s right)
	{
		auto r = std::static_pointer_cast<arraydata>(right->data());
		return std::make_shared<value>(r->size());
	}
	value_s compile_string(const virtualmachine* vm, value_s right)
	{
		auto r = right->as_string();
		auto cs = std::make_shared<callstack>();
		vm->parse_sqf(r, cs);
		return std::make_shared<value>(cs);
	}
	value_s typename_any(const virtualmachine* vm, value_s right)
	{
		return std::make_shared<value>(type_str(right->dtype()));
	}
	value_s str_any(const virtualmachine* vm, value_s right)
	{
		return std::make_shared<value>(std::make_shared<stringdata>(right->tosqf(), false), type::STRING);
	}
	value_s nil_(const virtualmachine* vm)
	{
		return std::make_shared<value>();
	}
	value_s comment_string(const virtualmachine* vm, value_s right)
	{
		return std::make_shared<value>();
	}
	value_s if_bool(const virtualmachine* vm, value_s right)
	{
		return std::make_shared<value>(right->data(), type::IF);
	}
	value_s then_if_array(const virtualmachine* vm, value_s left, value_s right)
	{
		auto ifcond = left->as_bool();
		auto arr = right->as_vector();
		if (arr.size() != 2)
		{
			vm->err() << L"Expected 2 elements in array." << std::endl;
			return std::make_shared<value>();
		}
		auto el0 = arr[0];
		auto el1 = arr[1];
		if (ifcond)
		{
			if (el1->dtype() != type::CODE)
			{
				vm->wrn() << L"Expected element 1 of array to be of type code." << std::endl;
			}
			if (el0->dtype() == type::CODE)
			{
				auto code = std::static_pointer_cast<codedata>(el0->data());
				code->loadinto(vm->stack());
				return value_s();
			}
			else
			{
				vm->err() << L"Expected element 0 of array to be of type code." << std::endl;
				return std::make_shared<value>();
			}
		}
		else
		{
			if (el0->dtype() != type::CODE)
			{
				vm->wrn() << L"Expected element 0 of array to be of type code." << std::endl;
			}
			if (el1->dtype() == type::CODE)
			{
				auto code = std::static_pointer_cast<codedata>(el1->data());
				code->loadinto(vm->stack());
				return value_s();
			}
			else
			{
				vm->err() << L"Expected element 1 of array to be of type code." << std::endl;
				return std::make_shared<value>();
			}
		}
	}
	value_s then_if_code(const virtualmachine* vm, value_s left, value_s right)
	{
		auto ifcond = left->as_bool();
		auto code = std::static_pointer_cast<codedata>(right->data());
		if (ifcond)
		{
			code->loadinto(vm->stack());
			return value_s();
		}
		else
		{
			return std::make_shared<value>();
		}
	}
	value_s else_code_code(const virtualmachine* vm, value_s left, value_s right)
	{
		auto vec = std::vector<value_s>(2);
		vec[0] = left;
		vec[1] = right;
		return std::make_shared<value>(vec);
	}
	value_s while_code(const virtualmachine* vm, value_s right)
	{
		return std::make_shared<value>(right->data(), type::WHILE);
	}
	value_s do_while_code(const virtualmachine* vm, value_s left, value_s right)
	{
		auto whilecond = std::static_pointer_cast<codedata>(left->data());
		auto execcode = std::static_pointer_cast<codedata>(right->data());

		auto cs = std::make_shared<callstack_while>(whilecond, execcode);
		vm->stack()->pushcallstack(cs);

		return value_s();
	}
	value_s for_string(const virtualmachine* vm, value_s right)
	{
		auto str = right->as_string();
		return std::make_shared<value>(std::make_shared<fordata>(str), type::FOR);
	}
	value_s from_for_scalar(const virtualmachine* vm, value_s left, value_s right)
	{
		auto fordata = std::static_pointer_cast<sqf::fordata>(left->data());
		auto index = right->as_double();
		fordata->from(index);
		return left;
	}
	value_s to_for_scalar(const virtualmachine* vm, value_s left, value_s right)
	{
		auto fordata = std::static_pointer_cast<sqf::fordata>(left->data());
		auto index = right->as_double();
		fordata->to(index);
		return left;
	}
	value_s step_for_scalar(const virtualmachine* vm, value_s left, value_s right)
	{
		auto fordata = std::static_pointer_cast<sqf::fordata>(left->data());
		auto index = right->as_double();
		fordata->step(index);
		return left;
	}
	value_s do_for_code(const virtualmachine* vm, value_s left, value_s right)
	{
		auto fordata = std::static_pointer_cast<sqf::fordata>(left->data());
		auto execcode = std::static_pointer_cast<codedata>(right->data());

		auto cs = std::make_shared<callstack_for>(fordata, execcode);
		vm->stack()->pushcallstack(cs);
		return value_s();
	}
	value_s select_array_scalar(const virtualmachine* vm, value_s left, value_s right)
	{
		auto arr = left->as_vector();
		auto index = right->as_int();

		if (arr.size() > index || index < 0)
		{
			vm->err() << L"Index out of range." << std::endl;
			return std::make_shared<value>();
		}
		if (arr.size() == index)
		{
			vm->wrn() << L"Index equals range. Returning nil." << std::endl;
			return std::make_shared<value>();
		}
		return arr[index];
	}
	value_s select_array_bool(const virtualmachine* vm, value_s left, value_s right)
	{
		auto arr = left->as_vector();
		auto flag = right->as_bool();
		if (!flag && arr.size() < 2 || arr.size() < 1)
		{
			vm->wrn() << L"Array should have at least two elements. Returning nil" << std::endl;
			return std::make_shared<value>();
		}
		else if (flag && arr.size() < 2)
		{
			vm->wrn() << L"Array should have at least two elements." << std::endl;
		}
		return flag ? arr[0] : arr[1];
	}
	value_s select_string_array(const virtualmachine* vm, value_s left, value_s right)
	{
		auto str = left->as_string();
		auto arr = right->as_vector();
		if (arr.size() < 1)
		{
			vm->err() << L"Array was expected to have at least a single element." << std::endl;
			return std::make_shared<value>();
		}
		if (arr[0]->dtype() != type::SCALAR)
		{
			vm->err() << L"First element of array was expected to be SCALAR, got " << sqf::type_str(arr[0]->dtype()) << L'.' << std::endl;
			return std::make_shared<value>();
		}
		int start = arr[0]->as_int();
		if (start < 0)
		{
			vm->wrn() << L"Start index is smaller then 0. Returning empty string." << std::endl;
			return std::make_shared<value>(L"");
		}
		if (start > (int)str.length())
		{
			vm->wrn() << L"Start index is larger then string length. Returning empty string." << std::endl;
			return std::make_shared<value>(L"");
		}
		if (arr.size() >= 2)
		{
			if (arr[1]->dtype() != type::SCALAR)
			{
				vm->err() << L"Second element of array was expected to be SCALAR, got " << sqf::type_str(arr[0]->dtype()) << L'.' << std::endl;
				return std::make_shared<value>();
			}
			int length = arr[1]->as_int();
			if (length < 0)
			{
				vm->wrn() << L"Length is smaller then 0. Returning empty string." << std::endl;
				return std::make_shared<value>(L"");
			}
			return std::make_shared<value>(str.substr(start, length));
		}
		return std::make_shared<value>(str.substr(start));
	}
	value_s select_array_array(const virtualmachine* vm, value_s left, value_s right)
	{
		auto vec = left->as_vector();
		auto arr = right->as_vector();
		if (arr.size() < 1)
		{
			vm->err() << L"Array was expected to have at least a single element." << std::endl;
			return std::make_shared<value>();
		}
		if (arr[0]->dtype() != type::SCALAR)
		{
			vm->err() << L"First element of array was expected to be SCALAR, got " << sqf::type_str(arr[0]->dtype()) << L'.' << std::endl;
			return std::make_shared<value>();
		}
		int start = arr[0]->as_int();
		if (start < 0)
		{
			vm->wrn() << L"Start index is smaller then 0. Returning empty array." << std::endl;
			return std::make_shared<value>(L"");
		}
		if (start > (int)vec.size())
		{
			vm->wrn() << L"Start index is larger then string length. Returning empty array." << std::endl;
			return std::make_shared<value>(L"");
		}
		if (arr.size() >= 2)
		{
			if (arr[1]->dtype() != type::SCALAR)
			{
				vm->err() << L"Second element of array was expected to be SCALAR, got " << sqf::type_str(arr[0]->dtype()) << L'.' << std::endl;
				return std::make_shared<value>();
			}
			int length = arr[1]->as_int();
			if (length < 0)
			{
				vm->wrn() << L"Length is smaller then 0. Returning empty array." << std::endl;
				return std::make_shared<value>(L"");
			}
			
			return std::make_shared<value>(std::vector<value_s>(vec.begin() + start, start + length > (int)vec.size() ? vec.end() : vec.begin() + start + length));
		}
		return std::make_shared<value>(std::vector<value_s>(vec.begin() + start, vec.end()));
	}
}
void sqf::commandmap::initgenericcmds(void)
{
	add(nular(L"nil", L"Nil value. This value can be used to undefine existing variables.", nil_));
	add(unary(L"call", sqf::type::CODE, L"Executes given set of compiled instructions.", call_code));
	add(binary(4, L"call", sqf::type::ANY, sqf::type::CODE, L"Executes given set of compiled instructions with an option to pass arguments to the executed Code.", call_any_code));
	add(unary(L"count", sqf::type::ARRAY, L"Can be used to count: the number of elements in array.", count_array));
	add(unary(L"compile", sqf::type::STRING, L"Compile expression.", compile_string));
	add(unary(L"typeName", sqf::type::ANY, L"Returns the data type of an expression.", typename_any));
	add(unary(L"str", sqf::type::ANY, L"Converts any value into a string.", str_any));
	add(unary(L"comment", sqf::type::STRING, L"Define a comment. Mainly used in SQF Syntax, as you're able to introduce comment lines with semicolons in a SQS script.", comment_string));
	add(unary(L"if", sqf::type::BOOL, L"This operator creates a If Type which is used in conjunction with the 'then' command.", if_bool));
	add(binary(4, L"then", type::IF, type::ARRAY, L"First or second element of array is executed depending on left arg. Result of the expression executed is returned as a result (result may be Nothing).", then_if_array));
	add(binary(4, L"then", type::IF, type::CODE, L"If left arg is true, right arg is executed. Result of the expression executed is returned as a result (result may be Nothing).", then_if_code));
	add(binary(5, L"else", type::CODE, type::CODE, L"Concats left and right element into a single, 2 element array.", else_code_code));
	add(unary(L"while", type::CODE, L"Marks code as WHILE type.", while_code));
	add(binary(4, L"do", type::WHILE, type::CODE, L"Executes provided code as long as while condition evaluates to true.", do_while_code));
	add(unary(L"for", type::STRING, L"Creates a FOR type for usage in 'for <var> from <start> to <end> [ step <stepsize> ] do <code>' construct.", for_string));
	add(binary(4, L"from", type::FOR, type::SCALAR, L"Sets the start index in a FOR type construct.", from_for_scalar));
	add(binary(4, L"to", type::FOR, type::SCALAR, L"Sets the end index in a FOR type construct.", to_for_scalar));
	add(binary(4, L"step", type::FOR, type::SCALAR, L"Sets the step size (default: 1) in a FOR type construct.", step_for_scalar));
	add(binary(4, L"do", type::FOR, type::CODE, L"Executes provided code as long as the var is smaller then the end index.", do_for_code));

	add(binary(4, L"select", type::ARRAY, type::SCALAR, L"Selects the element at provided index from array. If the index provided equals the array length, nil will be returned.", select_array_scalar));
	add(binary(4, L"select", type::ARRAY, type::BOOL, L"Selects the first element if provided boolean is true, second element if it is false.", select_array_bool));
	add(binary(4, L"select", type::STRING, type::ARRAY, L"Selects a range of characters in provided string, starting at element 0 index, ending at either end of the string or the provided element 1 length.", select_string_array));
	add(binary(4, L"select", type::ARRAY, type::ARRAY, L"Selects a range of elements in provided array, starting at element 0 index, ending at either end of the string or the provided element 1 length.", select_array_array));
	add(binary(4, L"select", type::ARRAY, type::CODE, L"", [](const virtualmachine* vm, value_s l, value_s r) -> value_s { vm->err() << L"NOT IMPLEMENTED (select)." << std::endl; return std::make_shared<value>(); }));
	add(binary(4, L"select", type::CONFIG, type::SCALAR, L"", [](const virtualmachine* vm, value_s l, value_s r) -> value_s { vm->err() << L"NOT IMPLEMENTED (select)." << std::endl; return std::make_shared<value>(); }));
}
