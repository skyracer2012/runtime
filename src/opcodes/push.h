#pragma once
#include "../runtime/instruction.h"
#include "../runtime/value.h"
#include "../runtime/data.h"
#include "../runtime/runtime.h"

namespace sqf::opcodes
{
	class push : public sqf::runtime::instruction
	{
	private:
		sqf::runtime::value m_value;

	public:
		push(sqf::runtime::value value) : m_value(value) {}
		virtual void execute(sqf::runtime::runtime& vm) const override { vm.active_context()->push_value(m_value); }
		virtual std::string to_string() const override { return std::string("PUSH ") + m_value.data<sqf::runtime::data>()->to_string_sqf(); }
		sqf::runtime::value::cref value() const { return m_value; }
		virtual std::optional<std::string> reconstruct(
			std::vector<sqf::runtime::instruction::sptr>::const_iterator& current,
			std::vector<sqf::runtime::instruction::sptr>::const_iterator end,
			short parent_precedence, bool left_from_binary) const override
		{
			return m_value.data<::sqf::runtime::data>()->to_string_sqf();
		}

		virtual bool equals(const instruction* p_other) const override
		{
			auto casted = dynamic_cast<const push*>(p_other);
			return casted != nullptr && casted->m_value == m_value;
		}
	};
}
