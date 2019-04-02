/*
	This file is part of solidity.

	solidity is free software: you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation, either version 3 of the License, or
	(at your option) any later version.

	solidity is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with solidity.  If not, see <http://www.gnu.org/licenses/>.
*/

#include <libsolidity/ast/TypeProvider.h>
#include <libdevcore/ArrayUtil.h>

using namespace std;
using namespace dev;
using namespace solidity;

template <size_t... N>
constexpr std::array<IntegerType, sizeof...(N)> createIntegerTypes(IntegerType::Modifier _modifier, std::index_sequence<N...>)
{
	return makeArray<IntegerType>(IntegerType((static_cast<unsigned>(N) + 1) * 8, _modifier)...);
}

template <size_t... N>
constexpr std::array<FixedBytesType, sizeof...(N)> createFixedBytesTypes(std::index_sequence<N...>)
{
	return makeArray<FixedBytesType>(FixedBytesType(static_cast<unsigned>(N) + 1)...);
}

BoolType const TypeProvider::m_boolType{};
InaccessibleDynamicType const TypeProvider::m_inaccessibleDynamicType{};
ArrayType const TypeProvider::m_bytesType{DataLocation::Storage, false};
ArrayType const TypeProvider::m_bytesMemoryType{DataLocation::Memory, false};
ArrayType const TypeProvider::m_stringType{DataLocation::Storage, true};
ArrayType const TypeProvider::m_stringMemoryType{DataLocation::Memory, true};
TupleType const TypeProvider::m_emptyTupleType{};
AddressType const TypeProvider::m_payableAddressType{StateMutability::Payable};
AddressType const TypeProvider::m_addressType{StateMutability::NonPayable};
std::array<IntegerType, 32> const TypeProvider::m_intM{createIntegerTypes(IntegerType::Modifier::Signed, std::make_index_sequence<32>{})};
std::array<IntegerType, 32> const TypeProvider::m_uintM{createIntegerTypes(IntegerType::Modifier::Unsigned, std::make_index_sequence<32>{})};
std::array<FixedBytesType, 32> const TypeProvider::m_bytesM{createFixedBytesTypes(std::make_index_sequence<32>{})};
std::array<MagicType, 4> const TypeProvider::m_magicTypes{
	MagicType{MagicType::Kind::Block},
	MagicType{MagicType::Kind::Message},
	MagicType{MagicType::Kind::Transaction},
	MagicType{MagicType::Kind::ABI}
	// MetaType is stored separately
};

TypeProvider::TypeProvider()
{
}

inline void clearCache(Type const& type)
{
	type.clearCache();
}

template <typename... Args>
inline void clearCache(Type const& type, Args&... moreTypes)
{
	clearCache(type);
	clearCache(moreTypes...);
}

template <typename Container>
inline void clearAllCaches(Container& container)
{
	for_each(begin(container), end(container), [](Type const& t) { t.clearCache(); });
}

template <typename Container, typename... Args>
inline void clearAllCaches(Container& types, Args&... more)
{
	clearAllCaches(types);
	clearAllCaches(more...);
}

void TypeProvider::reset()
{
	clearCache(
		m_boolType,
		m_inaccessibleDynamicType,
		m_bytesType,
		m_bytesMemoryType,
		m_stringType,
		m_stringMemoryType,
		m_emptyTupleType,
		m_payableAddressType,
		m_addressType
	);
	clearAllCaches(m_intM, m_uintM, m_bytesM, m_magicTypes);

	m_generalTypes.clear();
	m_stringLiteralTypes.clear();
	m_ufixedMxN.clear();
	m_fixedMxN.clear();
}

template <typename T, typename... Args>
inline T const* TypeProvider::createAndGet(Args&& ... _args)
{
	m_generalTypes.emplace_back(std::make_unique<T>(std::forward<Args>(_args)...));
	return static_cast<T const*>(m_generalTypes.back().get());
}

Type const* TypeProvider::fromElementaryTypeName(ElementaryTypeNameToken const& _type)
{
	solAssert(TokenTraits::isElementaryTypeName(_type.token()),
		"Expected an elementary type name but got " + _type.toString()
	);

	unsigned const m = _type.firstNumber();
	unsigned const n = _type.secondNumber();

	switch (_type.token())
	{
	case Token::IntM:
		return integerType(m, IntegerType::Modifier::Signed);
	case Token::UIntM:
		return integerType(m, IntegerType::Modifier::Unsigned);
	case Token::Byte:
		return byteType();
	case Token::BytesM:
		return fixedBytesType(m);
	case Token::FixedMxN:
		return fixedPointType(m, n, FixedPointType::Modifier::Signed);
	case Token::UFixedMxN:
		return fixedPointType(m, n, FixedPointType::Modifier::Unsigned);
	case Token::Int:
		return integerType(256, IntegerType::Modifier::Signed);
	case Token::UInt:
		return integerType(256, IntegerType::Modifier::Unsigned);
	case Token::Fixed:
		return fixedPointType(128, 18, FixedPointType::Modifier::Signed);
	case Token::UFixed:
		return fixedPointType(128, 18, FixedPointType::Modifier::Unsigned);
	case Token::Address:
		return addressType();
	case Token::Bool:
		return boolType();
	case Token::Bytes:
		return bytesType();
	case Token::String:
		return stringType();
	default:
		solAssert(
			false,
			"Unable to convert elementary typename " + _type.toString() + " to type."
		);
	}
}

StringLiteralType const* TypeProvider::stringLiteralType(std::string const& literal)
{
	auto i = m_stringLiteralTypes.find(literal);
	if (i != m_stringLiteralTypes.end())
		return i->second.get();
	else
		return m_stringLiteralTypes.emplace(literal, std::make_unique<StringLiteralType>(literal)).first->second.get();
}

FixedPointType const* TypeProvider::fixedPointType(unsigned m, unsigned n, FixedPointType::Modifier _modifier)
{
	auto& map = _modifier == FixedPointType::Modifier::Unsigned ? m_ufixedMxN : m_fixedMxN;

	auto i = map.find(make_pair(m, n));
	if (i != map.end())
		return i->second.get();

	return map.emplace(
		make_pair(m, n),
		make_unique<FixedPointType>(
			m,
			n,
			_modifier
		)
	).first->second.get();
}

TupleType const* TypeProvider::tupleType(std::vector<Type const*>&& members)
{
	if (members.empty())
		return &m_emptyTupleType;

	return createAndGet<TupleType>(move(members));
}

ReferenceType const* TypeProvider::withLocation(ReferenceType const* _type, DataLocation _location, bool _isPointer)
{
	if (_type->location() == _location && _type->isPointer() == _isPointer)
		return _type;

	m_generalTypes.emplace_back(_type->copyForLocation(_location, _isPointer));
	return static_cast<ReferenceType const*>(m_generalTypes.back().get());
}

FunctionType const* TypeProvider::functionType(FunctionDefinition const& _function, bool _isInternal)
{
	return createAndGet<FunctionType>(_function, _isInternal);
}

FunctionType const* TypeProvider::functionType(VariableDeclaration const& _varDecl)
{
	return createAndGet<FunctionType>(_varDecl);
}

FunctionType const* TypeProvider::functionType(EventDefinition const& _def)
{
	return createAndGet<FunctionType>(_def);
}

FunctionType const* TypeProvider::functionType(FunctionTypeName const& _typeName)
{
	return createAndGet<FunctionType>(_typeName);
}

FunctionType const* TypeProvider::functionType(
	strings const& _parameterTypes,
	strings const& _returnParameterTypes,
	FunctionType::Kind _kind,
	bool _arbitraryParameters,
	StateMutability _stateMutability
)
{
	return createAndGet<FunctionType>(
		_parameterTypes, _returnParameterTypes,
		_kind, _arbitraryParameters, _stateMutability
	);
}

FunctionType const* TypeProvider::functionType(
	TypePointers const& _parameterTypes,
	TypePointers const& _returnParameterTypes,
	strings _parameterNames,
	strings _returnParameterNames,
	FunctionType::Kind _kind,
	bool _arbitraryParameters,
	StateMutability _stateMutability,
	Declaration const* _declaration,
	bool _gasSet,
	bool _valueSet,
	bool _bound
)
{
	return createAndGet<FunctionType>(
		_parameterTypes,
		_returnParameterTypes,
		_parameterNames,
		_returnParameterNames,
		_kind,
		_arbitraryParameters,
		_stateMutability,
		_declaration,
		_gasSet,
		_valueSet,
		_bound
	);
}

RationalNumberType const* TypeProvider::rationalNumberType(rational const& _value, Type const* _compatibleBytesType)
{
	return createAndGet<RationalNumberType>(_value, _compatibleBytesType);
}

ArrayType const* TypeProvider::arrayType(DataLocation _location, bool _isString)
{
	if (_isString)
	{
		if (_location == DataLocation::Storage)
			return stringType();
		if (_location == DataLocation::Memory)
			return stringMemoryType();
	}
	else
	{
		if (_location == DataLocation::Storage)
			return bytesType();
		if (_location == DataLocation::Memory)
			return bytesMemoryType();
	}
	return createAndGet<ArrayType>(_location, _isString);
}

ArrayType const* TypeProvider::arrayType(DataLocation _location, Type const* _baseType)
{
	return createAndGet<ArrayType>(_location, _baseType);
}

ArrayType const* TypeProvider::arrayType(DataLocation _location, Type const* _baseType, u256 const& _length)
{
	return createAndGet<ArrayType>(_location, _baseType, _length);
}

ContractType const* TypeProvider::contractType(ContractDefinition const& _contractDef, bool _isSuper)
{
	return createAndGet<ContractType>(_contractDef, _isSuper);
}

EnumType const* TypeProvider::enumType(EnumDefinition const& _enumDef)
{
	return createAndGet<EnumType>(_enumDef);
}

ModuleType const* TypeProvider::moduleType(SourceUnit const& _source)
{
	return createAndGet<ModuleType>(_source);
}

TypeType const* TypeProvider::typeType(Type const* _actualType)
{
	return createAndGet<TypeType>(_actualType);
}

StructType const* TypeProvider::structType(StructDefinition const& _struct, DataLocation _location)
{
	return createAndGet<StructType>(_struct, _location);
}

ModifierType const* TypeProvider::modifierType(ModifierDefinition const& _def)
{
	return createAndGet<ModifierType>(_def);
}

MagicType const* TypeProvider::magicType(MagicType::Kind _kind)
{
	solAssert(_kind != MagicType::Kind::MetaType, "MetaType is handled separately");
	return &m_magicTypes.at(static_cast<size_t>(_kind));
}

MagicType const* TypeProvider::metaType(Type const* _type)
{
	solAssert(_type && _type->category() == Type::Category::Contract, "Only contracts supported for now.");
	return createAndGet<MagicType>(_type);
}

MappingType const* TypeProvider::mappingType(Type const* _keyType, Type const* _valueType)
{
	return createAndGet<MappingType>(_keyType, _valueType);
}
