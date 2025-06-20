#include "source/GcLib/pch.h"

#include "../GstdUtility.hpp"
#include "Script.hpp"
#include "ScriptLexer.hpp"

using namespace gstd;

//****************************************************************************
//script_type_manager
//****************************************************************************
script_type_manager* script_type_manager::base_ = nullptr;
script_type_manager::script_type_manager() {
	if (base_) return;
	base_ = this;

	null_type = deref_itr(types.insert(type_data(type_data::tk_null)).first);
	int_type = deref_itr(types.insert(type_data(type_data::tk_int)).first);
	float_type = deref_itr(types.insert(type_data(type_data::tk_float)).first);
	char_type = deref_itr(types.insert(type_data(type_data::tk_char)).first);
	boolean_type = deref_itr(types.insert(type_data(type_data::tk_boolean)).first);
	ptr_type = deref_itr(types.insert(type_data(type_data::tk_pointer)).first);

	null_array_type = deref_itr(types.insert(type_data(type_data::tk_array)).first);

	string_type = deref_itr(types.insert(type_data(type_data::tk_array,
		char_type)).first);		//Char array (string)
	int_array_type = deref_itr(types.insert(type_data(type_data::tk_array,
		int_type)).first);		//Int array
	float_array_type = deref_itr(types.insert(type_data(type_data::tk_array,
		float_type)).first);	//Real array

	//Bool array
	types.insert(type_data(type_data::tk_array, boolean_type));
	//String array
	types.insert(type_data(type_data::tk_array, string_type));
}

type_data* script_type_manager::get_type(type_data* type) {
	auto itr = types.find(*type);
	if (itr == types.end()) {
		//No type found, insert and return the new type
		itr = types.insert(*type).first;
	}
	return deref_itr(itr);
}
type_data* script_type_manager::get_type(type_data::type_kind kind) {
	type_data target = type_data(kind);
	return get_type(&target);
}
type_data* script_type_manager::get_array_type(type_data* element) {
	type_data target = type_data(type_data::tk_array, element);
	return get_type(&target);
}

//****************************************************************************
//script_engine
//****************************************************************************
script_engine::script_engine(const std::wstring& source, std::vector<function>* list_func, std::vector<constant>* list_const) {
	init(source.data(), source.data() + source.size(), list_func, list_const);
}
script_engine::script_engine(const std::vector<char>& source, std::vector<function>* list_func, std::vector<constant>* list_const) {
	const char* begin = source.data();
	const char* end = begin + source.size();
	init((wchar_t*)begin, (wchar_t*)end, list_func, list_const);
}
script_engine::script_engine(const wchar_t* source, const wchar_t* end, std::vector<function>* list_func, std::vector<constant>* list_const) {
	init(source, end, list_func, list_const);
}
script_engine::~script_engine() {
	blocks.clear();
}

void script_engine::init(const wchar_t* source, const wchar_t* end, std::vector<function>* list_func, std::vector<constant>* list_const) {
	main_block = new_block(1, block_kind::bk_normal);

	data = nullptr;

	script_scanner s(source, end);
	parser p(this, &s);

	if (list_func) p.load_functions(list_func);
	if (list_const) p.load_constants(list_const);
	p.begin_parse();

	events = p.events;

	error = p.error;
	error_message = p.error_message;
	error_line = p.error_line;
}

script_block* script_engine::new_block(int level, block_kind kind) {
	script_block x(level, kind);
	return &*blocks.insert(blocks.end(), x);
}

//****************************************************************************
//script_machine::environment
//****************************************************************************
script_machine::env_allocator::env_allocator(script_machine* machine) : machine(machine) {
	_alloc_more(1024);
}
script_machine::env_allocator::~env_allocator() {
	// Must release all owned environment before destroying the allocator
	environments.clear();
}

void script_machine::env_allocator::_alloc_more(size_t n) {
	constexpr size_t MAX_N = std::numeric_limits<size_t>::max() / sizeof(value_type);

	size_t before_size = environments.size();

	if (before_size + n > MAX_N)
		throw std::bad_array_new_length();

	for (size_t i = 0; i < n; ++i) {
		size_t index = before_size + i;

		environment env(machine);
		env._index = index;

		environments.push_back(std::move(env));
		free_environments.push_back(index);
	}
}

script_machine::env_allocator::value_type* 
script_machine::env_allocator::allocate(size_t n) {
	if (free_environments.size() == 0) {
		_alloc_more(1024);
	}

	size_t free_index = free_environments.front();
	free_environments.pop_front();

	return &environments[free_index];
}
void script_machine::env_allocator::deallocate(value_type* p, size_t n) noexcept {
	for (size_t i = 0; i < n; ++i) {
		value_type& env = p[i];

		env.variables.clear();
		env.stack.clear();

		free_environments.push_back(env._index);
	}
}

//****************************************************************************
//script_machine::environment
//****************************************************************************
script_machine::environment::environment(script_machine* machine) : 
	machine(machine), _index(-1),
	parent(parent),
	sub(sub), ip(0),
	hasResult(false), waitCount(0) {}

script_machine::environment::environment(const environment& base_from, sptr<environment> parent, script_block* sub) :
	environment(base_from.machine)
{
	_index = base_from._index;
	this->parent = parent;
	this->sub = sub;
}

//****************************************************************************
//script_machine
//****************************************************************************
script_machine::script_machine(script_engine* engine) : engine(engine), allocator(this) {
	reset();
}
script_machine::~script_machine() {
	reset();
}

script_machine::env_ptr script_machine::get_new_environment() {
	shared_ptr<environment> res;
	environment* pEnv = allocator.allocate(1);

	res.reset(pEnv, [this](environment* ptr) { 
		allocator.deallocate(ptr, 1);
	});
	return res;
}

bool script_machine::has_event(const std::string& event_name, std::map<std::string, script_block*>::iterator& res) {
	res = engine->events.find(event_name);
	return res != engine->events.end();
}
int script_machine::get_current_line() {
	//ref_unsync_ptr<environment>& current = *current_thread_index;
	//return (current->sub->codes[current->ip]).GetLine();
	return error_line;
}

void script_machine::reset() {
	error = false;
	error_message = L"";
	error_line = -1;

	bTerminate = false;

	finished = false;
	stopped = false;
	resuming = false;

	list_parent_environment.clear();
	threads.clear();
	current_thread_index = {};
}
void script_machine::run() {
	if (bTerminate) return;

	if (threads.size() == 0) {
		error_line = -1;

		env_ptr mainEnv = get_new_environment();
		*mainEnv = environment(*mainEnv, nullptr, engine->main_block);

		threads.push_back(mainEnv);

		current_thread_index = threads.begin();

		finished = false;
		stopped = false;
		resuming = false;

		run_code();
	}
}
void script_machine::resume() {
	if (bTerminate) return;

	stopped = false;
	finished = false;
	resuming = true;

	run_code();
}

void script_machine::call(const std::string& event_name) {
	call(engine->events.find(event_name));
}
void script_machine::call(std::map<std::string, script_block*>::iterator event_itr) {
	if (bTerminate) return;

	if (event_itr != engine->events.end()) {
		run();
		interrupt(event_itr->second);
	}
}

void script_machine::interrupt(script_block* sub) {
	// Save current thread
	auto prev_thread = current_thread_index;
	current_thread_index = threads.begin();

	// Replace current thread with the interrupt
	{
		env_ptr env_first = *current_thread_index;

		env_ptr new_env = get_new_environment();
		*new_env = environment(*new_env, env_first, sub);

		*current_thread_index = new_env;

		finished = false;

		list_parent_environment.push_back(new_env->parent->parent);
		run_code();
		list_parent_environment.pop_back();

		finished = false;
	}

	// Resume previous thread
	current_thread_index = prev_thread;
}
script_machine::env_ptr script_machine::add_thread(script_block* sub) {
	env_ptr e = get_new_environment();
	*e = environment(*e, *current_thread_index, sub);

	threads.insert(++current_thread_index, e);
	--current_thread_index;

	return MOVE(e);
}
script_machine::env_ptr script_machine::add_child_block(script_block* sub) {
	env_ptr e = get_new_environment();
	*e = environment(*e, *current_thread_index, sub);

	return *current_thread_index = MOVE(e);
}

void script_machine::run_code() {
	if (threads.size() == 0) {
		current_thread_index = {};
		return;
	}
	try {
		while (!finished && !bTerminate) {
			env_ptr current = *current_thread_index;

			if (current->waitCount > 0) {
				--(current->waitCount);
				yield();
				continue;
			}

			if (current->ip >= current->sub->codes.size()) {	// Routine finished
				env_ptr parent = current->parent;

				bool bFinish = false;
				if (parent == nullptr)	// Top-level env node
					bFinish = true;
				else if (list_parent_environment.size() > 1 && parent->parent != nullptr) {
					if (parent->parent == list_parent_environment.back())
						bFinish = true;
				}

				if (bFinish) {
					finished = true;
				}
				else {
					if (current->sub->kind == block_kind::bk_microthread) {
						current_thread_index = threads.erase(current_thread_index);
						yield();
					}
					else {
						if (current->hasResult && parent != nullptr)
							parent->stack.push_back(current->variables[0]);
						*current_thread_index = parent;
					}
				}
			}
			else {
				auto& stack = current->stack;
				auto& variables = current->variables;

				code* c = &(current->sub->codes[current->ip]);
				error_line = c->GetLine();
				++(current->ip);

				command_kind opc = c->GetOp();

				switch (opc) {
				case command_kind::pc_wait:
				{
					value* t = &stack.back();
					current->waitCount = (int)t->as_int() - 1;
					stack.pop_back();
					if (current->waitCount < 0) break;

					__fallthrough;
				}
				case command_kind::pc_yield:
					yield();
					break;

				case command_kind::pc_var_alloc:
					variables.resize(c->arg0);
					break;
				case command_kind::pc_var_format:
				{
					for (size_t i = c->arg0; i < c->arg0 + c->arg1; ++i) {
						if (i >= variables.capacity()) break;
						variables[i] = value();
					}
					break;
				}

				case command_kind::pc_pop:
					for (int i = 0; i < c->arg0; ++i)
						stack.pop_back();
					break;
				case command_kind::pc_push_value:
					stack.push_back(c->data);
					break;
				case command_kind::pc_push_variable:
				case command_kind::pc_push_variable2:
				{
					value* var = find_variable_symbol<false>(current, c, c->arg0, c->arg1);
					if (var == nullptr) break;

					if (opc == command_kind::pc_push_variable)
						stack.push_back(*var);
					else
						stack.push_back(value(script_type_manager::get_ptr_type(), var));

					break;
				}
				case command_kind::pc_dup_n:
				{
					if (c->arg0 >= stack.size()) break;
					value* val = &stack.back() - c->arg0;
					stack.push_back(*val);
					//stack.back().make_unique();
					break;
				}
				case command_kind::pc_swap:
				{
					size_t len = stack.size();
					if (len < 2) break;
					std::swap(stack[len - 1], stack[len - 2]);
					break;
				}
				case command_kind::pc_load_ptr:
				{
					if (c->arg0 >= stack.size()) break;
					value* val = &stack.back() - c->arg0;
					stack.push_back(value(script_type_manager::get_ptr_type(), val));
					break;
				}
				case command_kind::pc_unload_ptr:
				{
					value* val = &stack.back();
					value* valAtPtr = val->as_ptr();
					*val = *valAtPtr;
					break;
				}
				case command_kind::pc_make_unique:
				{
					if (c->arg0 >= stack.size()) break;
					value* val = &stack.back() - c->arg0;
					val->make_unique();
					break;
				}

				//case command_kind::_pc_jump_target:
				//	break;
				case command_kind::pc_jump:
					current->ip = c->arg0;
					break;
				case command_kind::pc_jump_if:
				case command_kind::pc_jump_if_not:
				{
					value* top = &stack.back();
					bool bJE = opc == command_kind::pc_jump_if;
					if ((bJE && top->as_boolean()) || (!bJE && !top->as_boolean()))
						current->ip = c->arg0;
					stack.pop_back();
					break;
				}
				case command_kind::pc_jump_if_nopop:
				case command_kind::pc_jump_if_not_nopop:
				{
					value* top = &stack.back();
					bool bJE = opc == command_kind::pc_jump_if_nopop;
					if ((bJE && top->as_boolean()) || (!bJE && !top->as_boolean()))
						current->ip = c->arg0;
					break;
				}

				case command_kind::pc_copy_assign:
				case command_kind::pc_ref_assign:
				{
					if (opc == command_kind::pc_copy_assign) {
						value* dest = find_variable_symbol<true>(current, c, c->arg0, c->arg1);
						value* src = &stack.back();

						if (dest != nullptr && src != nullptr) {
							// pc_copy_assign
							if (BaseFunction::_type_assign_check(this, src, dest)) {
								type_data* prev_type = dest->get_type();

								*dest = *src;
								dest->make_unique();

								if (prev_type && prev_type != src->get_type())
									BaseFunction::_value_cast(dest, prev_type);
							}
						}
						stack.pop_back();
					}
					else {		// pc_ref_assign
						value* src = &stack.back();
						value* dest = src[-1].as_ptr();

						if (dest != nullptr && src != nullptr) {
							if (BaseFunction::_type_assign_check(this, src, dest)) {
								type_data* prev_type = dest->get_type();

								*dest = *src;

								if (prev_type && prev_type != src->get_type())
									BaseFunction::_value_cast(dest, prev_type);
							}
						}
						stack.pop_back();
						stack.pop_back();
					}

					break;
				}

				case command_kind::pc_sub_return:
					for (env_ptr i = current; i != nullptr; i = i->parent) {
						i->ip = i->sub->codes.size();

						if (i->sub->kind == block_kind::bk_sub || i->sub->kind == block_kind::bk_function
							|| i->sub->kind == block_kind::bk_microthread)
							break;
					}
					break;
				case command_kind::pc_call:
				case command_kind::pc_call_and_push_result:
				{
					//assert(current_stack.size() >= c->arguments);

					if (stack.size() < c->arg1) {
						std::string error = StringUtility::Format(
							"Unexpected script error: Stack size[%d] is less than the number of arguments[%d].\r\n",
							stack.size(), c->arg1);
						raise_error(error);
						break;
					}

					bool bPushResult = opc == command_kind::pc_call_and_push_result;

					auto _ProcessReturn_BuiltinFunc = [&](value ret) {
						for (int i = 0; i < c->arg1; ++i)
							stack.pop_back();
						if (bPushResult)
							stack.push_back(ret);
					};
					auto _PassArgsFromStack = [](size_t argc, std::vector<value>& srcStk, std::vector<value>& dstStk) {
						for (int i = 0; i < argc; ++i) {
							dstStk.push_back(srcStk.back());
							srcStk.pop_back();
						}
					};

					script_block* sub = c->block; //(script_block*)c->arg0
					if (sub->func) {
						// Default functions

						size_t sizePrev = stack.size();

						value* argv = nullptr;
						if (sizePrev > 0 && c->arg1 > 0)
							argv = stack.data() + (sizePrev - c->arg1);

						if (sub->func != BaseFunction::invoke) {
							value ret = sub->func(this, c->arg1, argv);
							if (stopped) {
								--(current->ip);
							}
							else {
								resuming = false;
								_ProcessReturn_BuiltinFunc(ret);
							}
						}
						else {
							BaseFunction::invoke(this, c->arg1, argv);
							if (!error) {
								script_block* subIvk = (script_block*)(argv[0].as_int() & 0xffffffff);
								if (subIvk->func) {
									value ret = subIvk->func(this, subIvk->arguments, argv + 1);
									_ProcessReturn_BuiltinFunc(ret);
								}
								else if (subIvk->kind == block_kind::bk_microthread) {
									env_ptr e = add_thread(subIvk);

									_PassArgsFromStack(subIvk->arguments, stack, e->stack);
									stack.pop_back();

									if (bPushResult)
										stack.push_back(value());
								}
								else {
									env_ptr e = add_child_block(subIvk);
									e->hasResult = bPushResult;

									_PassArgsFromStack(subIvk->arguments, stack, e->stack);
									stack.pop_back();
								}
							}
						}
					}
					else if (sub->kind == block_kind::bk_microthread) {
						// Tasks
						env_ptr e = add_thread(sub);

						_PassArgsFromStack(c->arg1, stack, e->stack);
					}
					else {
						// User-defined functions or internal blocks
						env_ptr e = add_child_block(sub);
						e->hasResult = bPushResult;

						_PassArgsFromStack(c->arg1, stack, e->stack);
					}

					break;
				}

				case command_kind::pc_compare_e:
				case command_kind::pc_compare_g:
				case command_kind::pc_compare_ge:
				case command_kind::pc_compare_l:
				case command_kind::pc_compare_le:
				case command_kind::pc_compare_ne:
				{
					value* t = &stack.back();

					int r = t->as_int();
					bool b = false;

					switch (opc) {
					case command_kind::pc_compare_e:
						b = r == 0;
						break;
					case command_kind::pc_compare_g:
						b = r > 0;
						break;
					case command_kind::pc_compare_ge:
						b = r >= 0;
						break;
					case command_kind::pc_compare_l:
						b = r < 0;
						break;
					case command_kind::pc_compare_le:
						b = r <= 0;
						break;
					case command_kind::pc_compare_ne:
						b = r != 0;
						break;
					}
					t->reset(script_type_manager::get_boolean_type(), b);
					break;
				}

				// Loop commands
				case command_kind::pc_loop_ascent:
				case command_kind::pc_loop_descent:
				{
					value* cmp_arg = &stack.back() - 1;
					value cmp_res = BaseFunction::compare(this, 2, cmp_arg);

					bool bStopLoop = opc == command_kind::pc_loop_ascent ?
						(cmp_res.as_int() <= 0) : (cmp_res.as_int() >= 0);
					stack.push_back(value(script_type_manager::get_boolean_type(), bStopLoop));

					//stack.pop_back(2U);
					break;
				}
				case command_kind::pc_loop_count:
				{
					value* i = &stack.back();

					int64_t r = i->as_int();
					if (r > 0)
						i->reset(script_type_manager::get_int_type(), r - 1);
					
					stack.push_back(value(script_type_manager::get_boolean_type(), r > 0));

					break;
				}
				case command_kind::pc_loop_foreach:
				{
					// Stack: .... [array] [counter]
					value* i = &stack.back();
					value* src_array = i - 1;

					size_t index = i->as_int();
					size_t arrSize = src_array->length_as_array();

					bool bStopLoop = false;
					if (src_array->get_type()->get_kind() != type_data::tk_array || index >= arrSize) {
						bStopLoop = true;
					}
					else {
						stack.push_back(src_array->index_as_array(index));
						//stack.back().make_unique();
						i->set(i->get_type(), i->as_int() + 1LL);
					}

					stack.push_back(value(script_type_manager::get_boolean_type(), bStopLoop));
					break;
				}

				case command_kind::pc_construct_array:
				{
					if (c->arg0 == 0U) {
						stack.push_back(BaseFunction::_create_empty(script_type_manager::get_null_array_type()));
						break;
					}

					std::vector<value> res_arr;
					res_arr.resize(c->arg0);

					value* val_ptr = &stack.back() - c->arg0 + 1;

					type_data* type_elem = val_ptr->get_type();
					type_data* type_arr = script_type_manager::get_instance()->get_array_type(type_elem);

					value res;
					for (size_t iVal = 0U; iVal < c->arg0; ++iVal, ++val_ptr) {
						BaseFunction::_append_check(this, type_arr, val_ptr->get_type());
						{
							value appending = *val_ptr;
							if (appending.get_type()->get_kind() != type_elem->get_kind()) {
								appending.make_unique();
								BaseFunction::_value_cast(&appending, type_elem);
							}
							res_arr[iVal] = appending;
						}
					}
					res.reset(type_arr, res_arr);

					for (int i = 0; i < c->arg0; ++i)
						stack.pop_back();
					stack.push_back(res);
					break;
				}

#define ARG1_GET_LEVEL(_PK) (uint32_t)(((uint32_t)(_PK) & 0xfff00000) >> 20)
#define ARG1_GET_VAR(_PK)	(uint32_t)(((uint32_t)(_PK) & 0x000fffff))

				// ----------------------------------Inline operations----------------------------------
				case command_kind::pc_inline_inc:
				case command_kind::pc_inline_dec:
				{
					if (c->arg0) {
						value* var = find_variable_symbol<false>(current, c,
							ARG1_GET_LEVEL(c->arg1), ARG1_GET_VAR(c->arg1));
						if (var == nullptr) break;
						value res = (opc == command_kind::pc_inline_inc) ?
							BaseFunction::successor(this, 1, var) : BaseFunction::predecessor(this, 1, var);
						*var = res;
					}
					else {
						value* var = stack.back().as_ptr();
						if (!var->has_data()) break;
						value res = (opc == command_kind::pc_inline_inc) ?
							BaseFunction::successor(this, 1, var) : BaseFunction::predecessor(this, 1, var);
						*var = res;
						if (c->arg1)
							stack.pop_back();
					}
					break;
				}
				case command_kind::pc_inline_add_asi:
				case command_kind::pc_inline_sub_asi:
				case command_kind::pc_inline_mul_asi:
				case command_kind::pc_inline_div_asi:
				case command_kind::pc_inline_fdiv_asi:
				case command_kind::pc_inline_mod_asi:
				case command_kind::pc_inline_pow_asi:
				//case command_kind::pc_inline_cat_asi:
				{
					auto PerformFunction = [&](value* dest, command_kind cmd, value* argv) {
#define DEF_CASE(_c, _fn) case _c: *dest = BaseFunction::_fn(this, 2, argv); break;
						switch (cmd) {
							DEF_CASE(command_kind::pc_inline_add_asi, add);
							DEF_CASE(command_kind::pc_inline_sub_asi, subtract);
							DEF_CASE(command_kind::pc_inline_mul_asi, multiply);
							DEF_CASE(command_kind::pc_inline_div_asi, divide);
							DEF_CASE(command_kind::pc_inline_fdiv_asi, fdivide);
							DEF_CASE(command_kind::pc_inline_mod_asi, remainder_);
							DEF_CASE(command_kind::pc_inline_pow_asi, power);
							//DEF_CASE(command_kind::pc_inline_cat_asi, concatenate);
						}
#undef DEF_CASE
					};

					value res;
					if (c->arg0) {
						value* dest = find_variable_symbol<false>(current, c,
							ARG1_GET_LEVEL(c->arg1), ARG1_GET_VAR(c->arg1));
						if (dest == nullptr) break;

						value arg[2] = { *dest, stack.back() };
						PerformFunction(&res, opc, arg);

						BaseFunction::_value_cast(&res, dest->get_type());
						*dest = res;

						stack.pop_back();
					}
					else {
						value* pArg = &stack.back() - 1;
						value& pDest = *(pArg->as_ptr());

						value arg[2] = { pDest, pArg[1] };
						PerformFunction(&res, opc, arg);

						BaseFunction::_value_cast(&res, pDest.get_type());
						pDest = res;

						stack.pop_back();
						stack.pop_back();
					}
					break;
				}
				case command_kind::pc_inline_cat_asi:
				{
					if (c->arg0) {
						value* dest = find_variable_symbol<false>(current, c,
							ARG1_GET_LEVEL(c->arg1), ARG1_GET_VAR(c->arg1));
						if (dest == nullptr) break;

						value arg[2] = { *dest, stack.back() };
						BaseFunction::concatenate_direct(this, 2, arg);

						stack.pop_back();
					}
					else {
						value* pArg = &stack.back() - 1;

						value arg[2] = { *(pArg->as_ptr()), pArg[1] };
						BaseFunction::concatenate_direct(this, 2, arg);

						stack.pop_back();
						stack.pop_back();
					}
					break;
				}
				case command_kind::pc_inline_neg:
				case command_kind::pc_inline_not:
				case command_kind::pc_inline_abs:
				{
					value res;
					value* arg = &stack.back();

#define DEF_CASE(cmd, fn) case cmd: res = BaseFunction::fn(this, 1, arg); break;
					switch (opc) {
						DEF_CASE(command_kind::pc_inline_neg, negative);
						DEF_CASE(command_kind::pc_inline_not, not_);
						DEF_CASE(command_kind::pc_inline_abs, absolute);
					}
#undef DEF_CASE

					arg->make_unique();
					*arg = res;
					break;
				}
				case command_kind::pc_inline_add:
				case command_kind::pc_inline_sub:
				case command_kind::pc_inline_mul:
				case command_kind::pc_inline_div:
				case command_kind::pc_inline_fdiv:
				case command_kind::pc_inline_mod:
				case command_kind::pc_inline_pow:
				case command_kind::pc_inline_app:
				case command_kind::pc_inline_cat:
				{
					value res;
					value* args = &stack.back() - 1;

#define DEF_CASE(cmd, fn) case cmd: res = BaseFunction::fn(this, 2, args); break;
					switch (opc) {
						DEF_CASE(command_kind::pc_inline_add, add);
						DEF_CASE(command_kind::pc_inline_sub, subtract);
						DEF_CASE(command_kind::pc_inline_mul, multiply);
						DEF_CASE(command_kind::pc_inline_div, divide);
						DEF_CASE(command_kind::pc_inline_fdiv, fdivide);
						DEF_CASE(command_kind::pc_inline_mod, remainder_);
						DEF_CASE(command_kind::pc_inline_pow, power);
						DEF_CASE(command_kind::pc_inline_app, append);
						DEF_CASE(command_kind::pc_inline_cat, concatenate);
					}
#undef DEF_CASE

					//stack.pop_back(2U);
					//stack.push_back(res);
					stack.pop_back();
					stack.back() = res;
					break;
				}
				case command_kind::pc_inline_cmp_e:
				case command_kind::pc_inline_cmp_g:
				case command_kind::pc_inline_cmp_ge:
				case command_kind::pc_inline_cmp_l:
				case command_kind::pc_inline_cmp_le:
				case command_kind::pc_inline_cmp_ne:
				{
					value* args = &stack.back() - 1;

					value cmp_res = BaseFunction::compare(this, 2, args);
					int cmp_r = cmp_res.as_int();

					bool cmp_rb = false;

#define DEF_CASE(cmd, expr) case cmd: cmp_rb = (expr); break;
					switch (opc) {
						DEF_CASE(command_kind::pc_inline_cmp_e, cmp_r == 0);
						DEF_CASE(command_kind::pc_inline_cmp_g, cmp_r > 0);
						DEF_CASE(command_kind::pc_inline_cmp_ge, cmp_r >= 0);
						DEF_CASE(command_kind::pc_inline_cmp_l, cmp_r < 0);
						DEF_CASE(command_kind::pc_inline_cmp_le, cmp_r <= 0);
						DEF_CASE(command_kind::pc_inline_cmp_ne, cmp_r != 0);
					}
#undef DEF_CASE

					//stack.pop_back(2U);
					//stack.push_back(res);
					stack.pop_back();
					stack.back().reset(script_type_manager::get_boolean_type(), cmp_rb);

					break;
				}
				case command_kind::pc_inline_logic_and:
				case command_kind::pc_inline_logic_or:
				{
					value* var2 = &stack.back();
					value* var1 = var2 - 1;

					bool b = opc == command_kind::pc_inline_logic_and ?
						(var1->as_boolean() && var2->as_boolean()) : (var1->as_boolean() || var2->as_boolean());
					value res(script_type_manager::get_boolean_type(), b);

					//stack.pop_back(2U);
					//stack.push_back(res);
					stack.pop_back();
					stack.back() = res;
					break;
				}
				case command_kind::pc_inline_cast_var:
				{
					value* var = &stack.back();

					type_data* castFrom = var->get_type();
					type_data* castTo = (type_data*)c->arg0;

					if (c->arg1) {
						if (castTo && castFrom != castTo) {
							if (BaseFunction::_type_assign_check(this, castFrom, castTo)) {
								BaseFunction::_value_cast(var, castTo);
							}
						}
					}
					else {
						BaseFunction::_value_cast(var, castTo);
					}

					break;
				}
				case command_kind::pc_inline_index_array:
				{
					value* arr = &stack.back() - 1;
					value* idx = arr + 1;

					value* pRes = (value*)BaseFunction::index(this, 2, arr->as_ptr(), idx);
					if (pRes == nullptr) break;

					*arr = value(script_type_manager::get_ptr_type(), pRes);
					stack.pop_back();	//pop idx
					break;
				}
				case command_kind::pc_inline_index_array2:
				{
					value* arr = &stack.back() - 1;
					value* idx = arr + 1;

					value* pRes = (value*)BaseFunction::index(this, 2, arr, idx);
					if (pRes == nullptr) break;
					value res = *pRes;

					//stack.pop_back(2U);
					//stack.push_back(res);
					stack.pop_back();
					stack.back() = res;
					break;
				}
				case command_kind::pc_inline_length_array:
				{
					value* var = &stack.back();
					size_t len = var->length_as_array();
					var->reset(script_type_manager::get_int_type(), (int64_t)len);
					break;
				}
				}
			}

#undef ARG1_GET_LEVEL
#undef ARG1_GET_VAR
		}
	}
	catch (std::bad_alloc& e) {
		std::string error = StringUtility::Format("std: %s\r\n"
			"You shouldn't be seeing this. Please contact Natashi.\r\n", e.what());
		raise_error(error);
	}
}

template<bool ALLOW_NULL>
value* script_machine::find_variable_symbol(env_ptr current_env, code* c,
	uint32_t level, uint32_t variable)
{
	for (env_ptr i = current_env; i != nullptr; i = i->parent) {
		if (i->sub->level == level) {
			value* res = &(i->variables[variable]);

			if constexpr (ALLOW_NULL)
				return res;
			else {
				if (res->has_data())
					return res;
				else {
#ifdef _DEBUG
					raise_error(StringUtility::Format("Variable hasn't been initialized: %s\r\n",
						c->var_name.c_str()));
#else
					raise_error("Variable hasn't been initialized.\r\n");
#endif
					return nullptr;
				}
			}
		}
	}

#ifdef _DEBUG
	raise_error(StringUtility::Format("Variable not found: %s (level=%u,id=%u)\r\n",
		c->var_name.c_str(), level, variable));
#else
	raise_error(StringUtility::Format("Variable not found (level=%u,id=%u)\r\n",
		level, variable));
#endif
	return nullptr;
}