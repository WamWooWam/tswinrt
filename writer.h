#pragma once
#include <stack>
#include <algorithm>
#include <cctype>
#include <string>
#include <sstream>
#include <filesystem>
#include "winmd_reader.h"
#include "helpers.h"

using namespace winmd::reader;

class writer
{
private:
	std::set<std::string> _banned_identifiers{ "function", "arguments", "package" };

public:
	writer(std::vector<std::string> assemblies, std::filesystem::path& path) : _cache(assemblies), _path(path), _basePath(path), _out()
	{
		auto&& db = _cache.databases().front(); // grab the first database
		for (auto&& type : db.TypeDef)
		{
			if (!type.Flags().WindowsRuntime())
			{
				continue;
			}

			// store all the namespaces in it (these are the ones we're gonna process)
			_namespaces.emplace(type.TypeNamespace());
		}
	}

#pragma region generic stuff

	[[nodiscard]] auto push_generic_params(std::pair<GenericParam, GenericParam> const& range)
	{
		return _generic_args.push(range);
	}

	[[nodiscard]] auto push_generic_args(generic_type_instance const& type)
	{
		return _generic_args.push(type);
	}

	auto get_generic_arg_scope(uint32_t index)
	{
		return _generic_args.get(index);
	}

	auto get_generic_arg(uint32_t index)
	{
		return get_generic_arg_scope(index).first;
	}
#pragma endregion

	void write()
	{
		write_files();
		write_module();
	}

	void write_module()
	{
		_stack.clear();
		_path = _basePath;
		_out.open(_path.append("module.ts"), std::fstream::out | std::fstream::trunc);

		write_header();

		for (auto ns : _cache.namespaces())
		{
			std::string ns_name(ns.first);
			if (_namespaces.find(ns_name) == _namespaces.end())
				continue; // we're not processing types in this namespace

			for (auto&& [n, type] : ns.second.types)
			{
				if (is_exclusive_to(type) && !_include_exclusive)
					continue; // we're not processing IXXXStatics etc.

				if (has_attribute(type, "Windows.Foundation.Metadata", "WebHostHiddenAttribute") && !_allow_webhosthidden)
					continue; // no JS here

				std::string name{ n };
				auto index = name.find('`');
				if (index != std::string::npos)
					name = name.substr(0, index);

				std::string type_name = ns_name + "." + std::string(n);
				std::string name_override = ns_name + "." + name;
				std::replace(name_override.begin(), name_override.end(), '.', '_');
				write_import(type_name, name + " as " + name_override);
			}
		}

		_out << std::endl;

		auto push = [this](std::string& ns) {
			_out << whitespace(_stack.size());
			write_namespace_decl(ns);

			_stack.push_back(ns);
		};

		auto pop = [this]() {
			_stack.pop_back();
			_out << whitespace(_stack.size()) << "}" << std::endl;
		};

		for (auto ns : _cache.namespaces())
		{
			std::string ns_name(ns.first);
			if (_namespaces.find(ns_name) == _namespaces.end())
				continue; // we're not processing types in this namespace

			auto ns_bits = tokenise_string(ns_name, "."); // split by .
			while (_stack.size() > ns_bits.size())
				pop();

			for (int32_t i = min(ns_bits.size(), _stack.size()) - 1; i > 0; i--)
			{
				auto bit = ns_bits[i];
				if (!_stack.empty() && _stack.back() != bit)
					pop();
				else
					break;
			}

			for (size_t i = _stack.size(); i < ns_bits.size(); i++)
			{
				push(ns_bits[i]);
			}

			for (auto&& [n, type] : ns.second.types)
			{
				if (is_exclusive_to(type) && !_include_exclusive)
					continue;

				if (has_attribute(type, "Windows.Foundation.Metadata", "WebHostHiddenAttribute") && !_allow_webhosthidden)
					continue; // no JS here

				std::string name{ n };
				auto index = name.find('`');
				if (index != std::string::npos)
					name = name.substr(0, index);

				std::string name_override = ns_name + "." + name;
				std::replace(name_override.begin(), name_override.end(), '.', '_');

				std::string export_type = "type";
				auto category = get_category(type);
				if (category == category::class_type || category == category::enum_type)
					export_type = "const";

				auto guard{ _generic_args.push(type.GenericParam()) };
				auto generic_params = generic_type_params(type);
				_out << whitespace(_stack.size()) << "export " << export_type << " " << typedef_name(type, false) << generic_params << " = " << name_override << generic_params << ";" << std::endl;
			}
		}

		while (!_stack.empty())
		{
			pop();
		}

		_out << "globalThis['Windows'] = Windows;" << std::endl;
	}

	void write_files()
	{
		for (auto ns : _cache.namespaces())
		{
			std::string ns_name(ns.first);
			if (_namespaces.find(ns_name) == _namespaces.end())
				continue; // we're not processing types in this namespace

			auto ns_bits = tokenise_string(ns_name, "."); // split by .
			while (_stack.size() > ns_bits.size())
				pop();

			for (int32_t i = min(ns_bits.size(), _stack.size()) - 1; i > 0; i--)
			{
				auto bit = ns_bits[i];
				if (!_stack.empty() && _stack.back() != bit)
					pop();
				else
					break;
			}

			for (size_t i = _stack.size(); i < ns_bits.size(); i++)
			{
				_path.append(ns_bits[i]);
				_stack.push_back(ns_bits[i]);
			}

			if (!std::filesystem::is_directory(_path))
				std::filesystem::create_directories(_path);

			for (auto&& [name, type] : ns.second.types)
			{
				if (is_exclusive_to(type) && !_include_exclusive)
				{
					std::cout << "Skipping exlusive type " << type.TypeNamespace() << "." << type.TypeName() << std::endl;
					continue;
				}

				if (has_attribute(type, "Windows.Foundation.Metadata", "WebHostHiddenAttribute") && !_allow_webhosthidden)
				{
					std::cout << "Skipping WebHostHidden type " << type.TypeNamespace() << "." << type.TypeName() << std::endl;
					continue; // no JS here
				}

				auto type_name = std::string(type.TypeNamespace()) + "." + std::string(type.TypeName());
				auto file_name = std::string{ type.TypeName() } + ".ts";
				auto guard{ _generic_args.push(type.GenericParam()) };
				first_pass = true;

				do_write(type);

				_out.open(_path.append(file_name), std::fstream::out | std::fstream::trunc);

				write_header();

				if (_importedTypes.size() != 0)
				{
					for (auto&& imported_type : _importedTypes)
					{
						if (imported_type != type_name)
							write_import(imported_type);
					}

					_out << std::endl;
				}

				_stack.push_back(file_name);

				first_pass = false;
				do_write(type);
				pop();
			}
		}

		while (!_stack.empty())
		{
			pop();
		}
	}

	void do_write(TypeDef type)
	{
		switch (get_category(type))
		{
		case category::enum_type:
			write_enum(type);
			break;
		case category::struct_type:
			write_struct(type);
			break;
		case category::interface_type:
			write_interface(type);
			break;
		case category::class_type:
			write_class(type);
			break;
		case category::delegate_type:
			write_delegate(type);
			break;
		default:
			std::cout << "Not processing " << type.TypeNamespace() << "." << type.TypeName() << std::endl;
			break;
		}
	}

	void write_header()
	{
		_out << "// --------------------------------------------------" << std::endl;
		_out << "// <auto-generated>" << std::endl;
		_out << "//     This code was generated by a tool." << std::endl;
		_out << "// </auto-generated>" << std::endl;
		_out << "// --------------------------------------------------" << std::endl;
		_out << std::endl;
	}


	void write_namespace_decl(std::string_view name)
	{
		// write_whitespace(1);
		_out << "export namespace " << name << " {" << std::endl;
	}

	void write_enum(TypeDef type)
	{
		uint32_t val = 0;
		bool is_flags = has_attribute(type, "System", "FlagsAttribute");

		_out << "export enum " << type.TypeName() << " {" << std::endl;
		for (auto field : type.FieldList())
		{
			if (auto constant = field.Constant())
			{
				_out << whitespace(1) << normalise_member_name(field.Name());

				uint32_t constant_val = constant.ValueUInt32();
				if (constant_val != val || is_flags)
				{
					_out << " = " << constant_val;
				}

				_out << "," << std::endl;
				val++;
			}
		}

		_out << "}" << std::endl;
	}

	void write_struct(TypeDef type)
	{
		_out << "export interface " << type.TypeName() << " {" << std::endl;
		for (auto field : type.FieldList())
		{
			auto semantics = get_type_semantics(field.Signature().Type());
			_out << whitespace(1);

			if (field.Flags().Static())
				_out << "static ";

			_out << normalise_member_name(field.Name()) << ": " << projection_type_name(semantics, false) << ";" << std::endl;
		}

		_out << "}" << std::endl;
	}

	void write_interface(TypeDef type)
	{
		auto name = type_name(type, false);

		_out << "export interface " << name;
		write_inhereted_types(type, object_type{});
		_out << " {" << std::endl;

		write_properties(type);
		write_method_list(type, false);

		_out << "}" << std::endl;
	}

	void write_class(TypeDef type)
	{
		auto name = type_name(type, false);
		auto base_semantics = get_type_semantics(type.Extends());

		if (_generate_shims && _enable_decorators) {
			_importedTypes.insert("Windows.Foundation.Interop.GenerateShim");
			_out << "@GenerateShim('" << type.TypeNamespace() << "." << type.TypeName() << "')" << std::endl;
		}

		_out << "export class " << name;
		write_inhereted_types(type, base_semantics);
		_out << " { " << std::endl;

		write_properties(type);
		//	write_ctors(type, true);
		write_method_list(type, true);
		write_event_list(type);

		_out << "}" << std::endl;
	}

	void write_delegate(TypeDef type)
	{
		auto name = type_name(type, false);
		auto method = get_delegate_invoke(type);
		method_signature method_sig{ method };

		std::string return_type_name = "void";
		if (method_sig.return_signature())
		{
			auto return_type = get_type_semantics(method_sig.return_signature().Type());
			return_type_name = projection_type_name(return_type, false);
		}

		_out << "export type " << name << " = (";
		write_parameter_list(method_sig);
		_out << ") => " << return_type_name << ";" << std::endl;
	}

	void write_inhereted_types(TypeDef type, type_semantics semantics)
	{
		auto delimiter{ " extends " };
		auto write_delimiter = [&]() {
			_out << delimiter;
			delimiter = ", ";
		};

		if (!std::holds_alternative<object_type>(semantics))
		{
			write_delimiter();
			_out << type_name(semantics, false);
		}

		if (get_category(type) != category::interface_type)
			delimiter = " implements ";

		for (auto&& iface : type.InterfaceImpl())
		{
			for_typedef(get_type_semantics(iface.Interface()), [&](auto type) {
				if (!(is_exclusive_to(type) && !_include_exclusive))
				{
					write_delimiter();
					_out << type_name(type, false);
				}
			});
		}
	}

	void write_properties(TypeDef& type)
	{
		for (auto prop : type.PropertyList())
		{
			auto semantics = get_type_semantics(prop.Type().Type());
			auto [getter, setter] = get_property_methods(prop);
			auto is_static = false;

			_out << whitespace(1);
			if ((getter && getter.Flags().Static()) || (setter && setter.Flags().Static())) // technically impossible
			{
				is_static = true;
				_out << "static ";
			}

			if (is_static)
			{
				// retrieve property via reflection here
			}

			_out << normalise_member_name(prop.Name()) << ": " << projection_type_name(semantics, false) << ";" << std::endl;
		}
	}

	void write_ctors(TypeDef& type, bool include_signature)
	{
		std::vector<MethodDef> ctors;
		for (auto& method : type.MethodList())
		{
			auto name = method.Name();
			if (!method.Flags().SpecialName())
				continue;
			if (name == ".ctor")
				ctors.push_back(method);
		}

		std::sort(ctors.begin(), ctors.end(), [](const MethodDef& lhs, const MethodDef& rhs) { return distance(lhs.ParamList()) < distance(rhs.ParamList()); });

		int32_t max_dist = 0;
		for (auto& ctor : ctors)
		{
			max_dist = max(distance(ctor.ParamList()), max_dist);
		}

		int32_t min_dist = max_dist;
		for (auto& ctor : ctors)
		{
			min_dist = min(min_dist, distance(ctor.ParamList()));
		}

		if (max_dist == 0 && min_dist == 0)
			return;

		for (auto& ctor : ctors)
		{
			method_signature ctor_sig{ ctor };
			auto dist = distance(ctor.ParamList());

			_out << whitespace(1) << "constructor(";

			bool first = true;
			for (auto i = 0; i < dist; i++)
			{
				if (!first)
					_out << ", ";
				first = false;

				auto& param = ctor_sig.params()[i];
				auto param_type = get_type_semantics(param.second->Type());
				auto param_type_name = projection_type_name(param_type, false);

				_out << normalise_member_name(param.first.Name()) << ": " << param_type_name;

				if (dist == max_dist)
				{
					_out << " = null";
				}
			}
			_out << ")";

			if (dist == max_dist && include_signature)
			{
				_out << " {" << std::endl;
				_out << whitespace(2) << "throw new Error('not implemented')" << std::endl;
				_out << whitespace(1) << "}" << std::endl;
			}
			else
			{
				_out << ";" << std::endl;
			}
		}
	}

	void write_method_list(TypeDef& type, bool include_signature)
	{
		std::set<std::string> methods;
		for (auto& method : type.MethodList())
		{
			auto name = method.Name();
			if (method.Flags().SpecialName())
				continue;

			method_signature method_sig{ method };
			std::string return_type_name = "void";
			if (method_sig.return_signature())
			{
				auto return_type = get_type_semantics(method_sig.return_signature().Type());
				return_type_name = projection_type_name(return_type, false);
			}

			auto overload_attribute = get_attribute(method, "Windows.Foundation.Metadata", "OverloadAttribute");
			if (overload_attribute)
			{
				auto sig = std::get<ElemSig>(overload_attribute.Value().FixedArgs()[0].value);
				name = std::get<std::string_view>(sig.value);

				if (!first_pass)
					std::cout << "Overloading " << type.TypeNamespace() << "." << type.TypeName() << "#" << method.Name() << " -> " << type.TypeNamespace() << "." << type.TypeName() << "#" << name << std::endl;
			}

			auto method_name = normalise_member_name(name);
			if (methods.find(method_name) != methods.end()) {
				std::cout << "Skipping non-uniquely overloaded method " << type.TypeNamespace() << "." << type.TypeName() << "#" << name << std::endl;
				continue;
			}

			methods.insert(method_name);

			_out << whitespace(1);
			if (method.Flags().Static())
			{
				_out << "static ";
			}

			_out << method_name << "(";

			write_parameter_list(method_sig);

			_out << "): " << return_type_name;

			if (include_signature)
			{
				_out << " {" << std::endl;
				_out << whitespace(2) << "throw new Error('not implemented')" << std::endl;
				_out << whitespace(1) << "}" << std::endl;
			}
			else
			{
				_out << ";" << std::endl;
			}
		}
	}

	void write_event_list(TypeDef& type)
	{
		if (distance(type.EventList()) == 0)
			return;

		_out << std::endl;

		for (auto& event : type.EventList())
		{
			auto event_type = get_type_semantics(event.EventType());
			auto event_type_name = type_name(event_type, false);
			auto event_name = normalise_member_name(event.Name());
			auto array_name = "__" + normalise_member_name(event.Name());
			auto [add, remove] = get_event_methods(event);

			std::transform(event_name.begin(), event_name.end(), event_name.begin(), ::tolower);

			_out << whitespace(1) << "private " << array_name << ": "
				<< "Set<" << event_type_name << "> = new Set();" << std::endl;

			if (_enable_decorators)
			{
				_importedTypes.insert("Windows.Foundation.Interop.Enumerable");
				_out << whitespace(1) << "@Enumerable(true)" << std::endl;
			}

			_out << whitespace(1) << "set on" << event_name << "(handler: " << event_type_name << ") {" << std::endl;
			_out << whitespace(2) << "this." << array_name << ".add(handler);" << std::endl;
			_out << whitespace(1) << "}" << std::endl;
			_out << std::endl;
		}

		write_event_listener_function(type, "add", "add");
		_out << std::endl;
		write_event_listener_function(type, "remove", "delete");
	}

	void write_event_listener_function(winmd::reader::TypeDef& type, const std::string_view& name, const std::string_view& method)
	{
		_out << whitespace(1) << name << "EventListener(name: string, handler: any) {" << std::endl;
		_out << whitespace(2) << "switch (name) {" << std::endl;

		for (auto& event : type.EventList())
		{
			auto event_type = get_type_semantics(event.EventType());
			auto event_type_name = type_name(event_type, false);
			auto event_name = normalise_member_name(event.Name());
			auto array_name = "__" + normalise_member_name(event.Name());

			std::transform(event_name.begin(), event_name.end(), event_name.begin(), ::tolower);

			_out << whitespace(3) << "case '" << event_name << "':" << std::endl;
			_out << whitespace(4) << "this." << array_name << "." << method << "(handler);" << std::endl;
			_out << whitespace(4) << "break;" << std::endl;
		}

		auto super_semantics = get_type_semantics(type.Extends());
		if (type.Extends() && type.Extends().TypeDef() && distance(type.Extends().TypeDef().EventList()) > 0)
		{
			_out << whitespace(3) << "default:" << std::endl;
			_out << whitespace(4) << "super." << name << "EventListener(name, handler);" << std::endl;
			_out << whitespace(4) << "break;" << std::endl;
		}

		_out << whitespace(2) << "}" << std::endl;
		_out << whitespace(1) << "}" << std::endl;
	}

	void write_parameter_list(method_signature& method_sig)
	{
		bool first = true;
		for (auto& param : method_sig.params())
		{
			if (!first)
				_out << ", ";
			first = false;

			auto param_type = get_type_semantics(param.second->Type());
			auto param_type_name = projection_type_name(param_type, false);

			_out << normalise_member_name(param.first.Name()) << ": " << param_type_name;
		}
	}

	void write_import(const std::string& type_name, const std::string& name_override = "")
	{
		auto type = _cache.find(type_name);
		if (static_cast<bool>(type) && ((is_exclusive_to(type) && !_include_exclusive) || (has_attribute(type, "Windows.Foundation.Metadata", "WebHostHiddenAttribute") && !_allow_webhosthidden)))
		{
			// assign any to direct references to unprojected types
			_out << "type " << type.TypeName() << " = any" << std::endl;;
		}
		else
		{
			// we assume non-existent types are synthesised after the fact
			// generally required for decorators

			std::vector<std::string> type_bits = tokenise_string(type_name, "."); // split by .
			std::filesystem::path current_path = std::filesystem::absolute(_path).parent_path();
			std::filesystem::path abs_path(_basePath);
			for (auto type_bit : type_bits)
			{
				abs_path.append(type_bit);
			}

			auto path = std::filesystem::relative(abs_path, current_path);
			auto path_str = path.string();
			std::replace(path_str.begin(), path_str.end(), '\\', '/');

			if (path_str.rfind('.', 0) != 0)
			{
				path_str = "./" + path_str;
			}

			auto name = name_override.empty() ? type_bits.back() : name_override;
			auto index = name.find('`');
			if (index != std::string::npos)
				name = name.substr(0, index);

			_out << "import { " << name << " } from \"" << path_str << "\";" << std::endl;
		}
	}


	[[no_discard]] std::string whitespace(size_t depth)
	{
		return std::string(depth * 4, ' ');
	}

	void write_pop()
	{
		if (_out.is_open())
			_out.close();
		_path = _path.parent_path();
	}

	void pop()
	{
		_importedTypes.clear();
		_stack.pop_back();
		write_pop();
	}

	template <typename T>
	bool has_attribute(T const& row, std::string_view const& type_namespace, std::string_view const& type_name)
	{
		return static_cast<bool>(get_attribute(row, type_namespace, type_name));
	}

	std::vector<std::string> tokenise_string(std::string s, std::string delimiter)
	{
		std::vector<std::string> tokens;
		size_t position = 0;
		while ((position = s.find(delimiter)) != std::string::npos)
		{
			tokens.push_back(s.substr(0, position));
			s.erase(0, position + delimiter.length());
		}

		if (!s.empty())
			tokens.push_back(s);

		return tokens;
	}

	std::string normalise_member_name(const std::string_view& n)
	{
		std::string name{ n };
		if (isupper(name[0]))
		{
			size_t i = 0;
			for (; i < name.length(); i++)
			{
				if (name[i] == '_' || islower(name[i]))
				{
					break;
				}
			}

			std::string start = name.substr(0, i);
			std::transform(start.begin(), start.end(), start.begin(), ::tolower);
			std::string end = name.substr(i);

			return start + end;
		}

		if (_banned_identifiers.find(name) != _banned_identifiers.end())
		{
			name = "__" + name;
		}

		return name;
	}

	template <typename TAction, typename TResult = std::invoke_result_t<TAction, type_definition>>
	TResult for_typedef(type_semantics const& semantics, TAction action)
	{
		return call(
			semantics,
			[&](type_definition const& type) {
			return action(type);
		},
			[&](generic_type_instance const& type) {
			auto guard{ _generic_args.push(type) };
			return action(type.generic_type);
		},
			[](auto) {
			throw_invalid("type definition expected");
#pragma warning(disable : 4702)
			return TResult();
		});
	}

	std::string typedef_name(type_definition const& type, bool relative)
	{
		_importedTypes.insert(std::string(type.TypeNamespace()) + "." + std::string(type.TypeName()));

		std::stringstream s;
		std::vector<std::string> type_bits = tokenise_string(std::string(type.TypeNamespace()), ".");
		std::string name{ type.TypeName() };
		auto index = name.find('`');
		if (index != std::string::npos)
			name = name.substr(0, index);

		if (!relative)
			return name;

		type_bits.push_back(name);

		size_t i = 0;
		while (i < _stack.size() && i < type_bits.size() && type_bits[i] == _stack[i])
			i++;

		for (size_t x = i; x < type_bits.size(); x++)
		{
			s << type_bits[x];
			if (x != type_bits.size() - 1)
				s << ".";
		}

		s << generic_type_params(type);

		return s.str();
	}

	std::string fundamental_type_name(fundamental_type const& type)
	{
		switch (type)
		{
		case fundamental_type::Boolean:
			return "boolean";
		case fundamental_type::Char:
		case fundamental_type::String:
			return "string";
		default:
			return "number";
		}
	}

	std::string generic_type_name(generic_type_index const& var, bool relative)
	{
		return projection_type_name(get_generic_arg_scope(var.index).first, relative);
	}

	std::string generic_type_params(TypeDef const& type)
	{
		std::stringstream s;
		size_t dist = distance(type.GenericParam());
		if (dist == 0)
			return s.str();
		s << "<";
		for (size_t i = 0; i < dist; i++)
		{
			if (i > 0)
				s << ", ";

			s << projection_type_name(get_generic_arg(i), false);
		}
		s << ">";

		return s.str();
	}

	std::string generic_type_instance_name(generic_type_instance const& type, bool relative)
	{
		std::stringstream s;
		auto guard{ push_generic_args(type) };
		auto first = true;
		s << projection_type_name(type.generic_type, relative) << "<";
		for (auto x : type.generic_args)
		{
			if (!first)
				s << ", ";

			try
			{
				// TODO: figure out why this breaks
				s << projection_type_name(x, relative);
			}
			catch (const std::exception&)
			{
				break;
			}

			first = false;
		}

		s << ">";

		return s.str();
	}

	std::string projection_type_name(type_semantics const& s, bool relative)
	{
		return call(
			s,
			[&](object_type) { return std::string("any"); },
			[&](guid_type) { return std::string("string"); },
			[&](type_type) { return std::string("any"); },
			[&](type_definition const& type) { return typedef_name(type, relative); },
			[&](generic_type_index const& var) { return generic_type_name(var, relative); },
			[&](generic_type_instance const& type) { return generic_type_instance_name(type, relative); },
			[&](generic_type_param const& param) { return std::string(param.Name()); },
			[&](fundamental_type const& type) { return fundamental_type_name(type); });
	}

	std::string type_name(type_semantics const& semantics, bool relative)
	{
		return for_typedef(semantics, [&](auto type) {
			std::stringstream s;
			s << typedef_name(type, relative) << generic_type_params(type);
			return s.str();
		});
	}

private:
	cache _cache{};
	std::set<std::string> _namespaces{};
	std::set<std::string> _importedTypes{};
	std::vector<std::string> _stack{};
	std::filesystem::path& _path;
	const std::filesystem::path _basePath;
	std::fstream _out;
	std::fstream _module;
	generic_args _generic_args;
	bool first_pass;
	bool _enable_decorators = true;
	bool _generate_shims = true;
	bool _include_exclusive = false;
	bool _allow_webhosthidden = false;
};