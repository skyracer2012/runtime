#pragma once
#include "../runtime/instruction.h"
#include "../runtime/value.h"
#include "../runtime/data.h"
#include "../runtime/runtime.h"
#include "../runtime/sqfop.h"

namespace sqf::opcodes
{
	class call_nular : public sqf::runtime::instruction
	{
	private:
		std::string m_operator_name;

	public:
		call_nular(std::string key) : m_operator_name(key) {}
		virtual void execute(sqf::runtime::runtime& vm) const override
		{
			auto context = vm.active_context();
			sqf::runtime::sqfop_nular::key key = { m_operator_name };
			if (!vm.sqfop_exists(key))
			{
				vm.__logmsg(logmessage::runtime::UnknownInputTypeCombinationNular(diag_info(), key.name));
				return;
			}
			auto op = vm.sqfop_at(key);
			auto return_value = op.execute(vm);

			context->push_value(return_value);
		}
		virtual std::string to_string() const override { return std::string("CALLNULAR ") + m_operator_name; }
		std::string_view operator_name() const { return m_operator_name; }
		virtual std::optional<std::string> reconstruct(
			std::vector<sqf::runtime::instruction::sptr>::const_iterator& current,
			std::vector<sqf::runtime::instruction::sptr>::const_iterator end,
			short parent_precedence, bool left_from_binary) const override
		{
			return m_operator_name;
		}

		virtual bool equals(const instruction* p_other) const override
		{
			auto casted = dynamic_cast<const call_nular*>(p_other);
			return casted != nullptr && casted->m_operator_name == m_operator_name;
		}
	};
}