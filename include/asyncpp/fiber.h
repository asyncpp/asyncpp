#pragma once
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <exception>
#include <memory>
#include <optional>
#include <stdexcept>
#include <utility>

#include <asyncpp/detail/sanitizers.h>
#include <asyncpp/detail/std_import.h>

#ifndef ASYNCPP_FIBER_KEYWORDS
#define ASYNCPP_FIBER_KEYWORDS 1
#endif

#ifndef _WIN32

#include <sys/mman.h>
#include <unistd.h>

#ifndef ASYNCPP_FIBER_USE_UCONTEXT
#define ASYNCPP_FIBER_USE_UCONTEXT 0
#endif

#if ASYNCPP_FIBER_USE_UCONTEXT
#include <ucontext.h>
#endif

#if !defined(MAP_ANON) && defined(__APPLE__)
#define MAP_ANON 0x1000
#endif

namespace asyncpp::detail {
	struct stack_context {
		void* stack;
		size_t stack_size;
		void* mmap_base;
		size_t mmap_size;
	};

	inline bool fiber_allocate_stack(stack_context& ctx, size_t size) noexcept {
		static size_t pagesize = sysconf(_SC_PAGESIZE);

		// Round the stacksize to the next multiple of pages
		const auto page_count = (size + pagesize - 1) / pagesize;
		size = page_count * pagesize;
		const auto alloc_size = size + pagesize * 2;
#if defined(MAP_STACK)
		void* const stack =
			::mmap(nullptr, alloc_size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON | MAP_STACK, -1, 0);
#elif defined(MAP_ANON)
		void* const stack = ::mmap(nullptr, alloc_size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);
#else
		void* const stack = ::mmap(nullptr, alloc_size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
#endif
		if (stack == MAP_FAILED) return false;

		// Protect a page at the bottom and top to cause fault on stack over/underflow
		[[maybe_unused]] auto res = ::mprotect(stack, pagesize, PROT_NONE);
		assert(res == 0);
		res = ::mprotect(static_cast<std::byte*>(stack) + size + pagesize, pagesize, PROT_NONE);
		assert(res == 0);

		ctx.stack = static_cast<std::byte*>(stack) + size + pagesize;
		ctx.stack_size = size;
		ctx.mmap_base = stack;
		ctx.mmap_size = alloc_size;
		return true;
	}

	inline bool fiber_deallocate_stack(stack_context& ctx) {
		if (ctx.mmap_base == nullptr) return true;

		auto res = ::munmap(ctx.mmap_base, ctx.mmap_size);
		return res == 0;
	}

#if ASYNCPP_FIBER_USE_UCONTEXT
	struct fiber_context {
		ucontext_t context;
		void* stack;
		size_t stack_size;
	};

	inline bool fiber_makecontext(fiber_context* ctx, const stack_context& stack, void (*fn)(void* arg), void* arg) {
		static void (*const wrapper)(uint32_t, uint32_t, uint32_t, uint32_t) = [](uint32_t fn_hi, uint32_t fn_low,
																				  uint32_t arg_hi, uint32_t arg_low) {
			uintptr_t fn = (static_cast<uintptr_t>(fn_hi) << 32) | static_cast<uintptr_t>(fn_low);
			uintptr_t arg = (static_cast<uintptr_t>(arg_hi) << 32) | static_cast<uintptr_t>(arg_low);
			reinterpret_cast<void (*)(void*)>(fn)(reinterpret_cast<void*>(arg));
		};
		getcontext(&ctx->context);
		ctx->context.uc_link = nullptr;
		ctx->context.uc_stack.ss_sp = reinterpret_cast<decltype(ctx->context.uc_stack.ss_sp)>(
			reinterpret_cast<uintptr_t>(stack.stack) - stack.stack_size);
		ctx->context.uc_stack.ss_size = stack.stack_size;
		makecontext(&ctx->context, reinterpret_cast<void (*)()>(wrapper), 4,
					(reinterpret_cast<uintptr_t>(fn) >> 32) & 0xffffffff, reinterpret_cast<uintptr_t>(fn) & 0xffffffff,
					(reinterpret_cast<uintptr_t>(arg) >> 32) & 0xffffffff,
					reinterpret_cast<uintptr_t>(arg) & 0xffffffff);
		return true;
	}

	inline bool fiber_swapcontext(fiber_context* out, fiber_context* in) {
		swapcontext(&out->context, &in->context);
		return true;
	}

	inline bool fiber_destroy_context(fiber_context* ctx) {
		memset(ctx, 0, sizeof(fiber_context));
		return true;
	}
#else
	struct fiber_context {
		void* current_sp;
		void* stack;
		size_t stack_size;
	};

	inline bool fiber_makecontext(fiber_context* ctx, const stack_context& stack, void (*entry_fn)(void* arg),
								  void* arg) {
		ctx->stack = stack.stack;
		ctx->stack_size = stack.stack_size;

		// Make sure top of the stack is aligned to 16byte. This should always be the case but better safe than sorry.
		// NOLINTNEXTLINE(performance-no-int-to-ptr,readability-identifier-length)
		auto sp = reinterpret_cast<uintptr_t*>(reinterpret_cast<uintptr_t>(stack.stack) & static_cast<uintptr_t>(~15L));
#if defined(__i386__)
		// The restore code adds 28 byte and ret another 4. Since we want
		// ($esp & 0xf) == 0xc (as would be the case after a call),
		// we need to make sure this is already the case.
		sp -= 3;
		struct stack_frame {
			uint32_t mmx_cw;
			uint32_t x87_cw;
			uintptr_t stack_guard;
			uintptr_t edi;
			uintptr_t esi;
			uintptr_t ebx;
			uintptr_t ebp;
			uintptr_t ret_addr;
			// This would normally be the return address of the
			// called function if it was called via `call`.
			// However because we abuse a ret to jump to the entry
			// its not pushed.
			uintptr_t : sizeof(uintptr_t) * 8;
			uintptr_t param;
		};
		static_assert(sizeof(stack_frame) == sizeof(uintptr_t) * 10);
		sp -= sizeof(stack_frame) / sizeof(uintptr_t);
		memset(sp, 0, reinterpret_cast<uintptr_t>(top_of_stack) - reinterpret_cast<uintptr_t>(sp));
		auto frame = reinterpret_cast<stack_frame*>(sp);
		frame->ret_addr = reinterpret_cast<uintptr_t>(entry_fn);
		frame->param = reinterpret_cast<uintptr_t>(arg);
		assert((reinterpret_cast<uintptr_t>(sp) % 16) == 0xc);
#elif defined(__x86_64__)
		// The restore code adds 72 byte and ret another 8. Since we want
		// ($esp & 0xf) == 0x8 (as would be the case after a call),
		// we need to make sure this is already the case.
		sp -= 1;
		struct stack_frame {
			uint32_t mmx_cw;
			uint32_t x87_cw;
			uintptr_t stack_guard;
			uintptr_t r12;
			uintptr_t r13;
			uintptr_t r14;
			uintptr_t r15;
			uintptr_t rbx;
			uintptr_t rbp;
			union {
				uintptr_t rdi;
				uintptr_t param;
			};
			uintptr_t ret_addr;
		};
		static_assert(sizeof(stack_frame) == sizeof(uintptr_t) * 10);
		sp -= sizeof(stack_frame) / sizeof(uintptr_t);
		memset(sp, 0, reinterpret_cast<uintptr_t>(stack.stack) - reinterpret_cast<uintptr_t>(sp));
		auto frame = reinterpret_cast<stack_frame*>(sp);
		frame->ret_addr = reinterpret_cast<uintptr_t>(entry_fn);
		frame->param = reinterpret_cast<uintptr_t>(arg);
#elif defined(__arm__)
		// Stack needs to be 64bit aligned for vstmia
		sp -= 16;
		struct stack_frame {
#if (defined(__VFP_FP__) && !defined(__SOFTFP__))
			uintptr_t d8_d15[16];
			uintptr_t _padding_;
#endif
			union {
				uintptr_t r0;
				uintptr_t param;
			};
			uintptr_t r11;
			uintptr_t r10;
			uintptr_t r9;
			uintptr_t r8;
			uintptr_t r7;
			uintptr_t r6;
			uintptr_t r5;
			uintptr_t r4;
			uintptr_t r12;
			union {
				uintptr_t r14;
				uintptr_t ret_addr;
			};
		};
#if (defined(__VFP_FP__) && !defined(__SOFTFP__))
		static_assert(sizeof(stack_frame) == sizeof(uintptr_t) * 28);
#else
		static_assert(sizeof(stack_frame) == sizeof(uintptr_t) * 11);
#endif
		sp -= sizeof(stack_frame) / sizeof(uintptr_t);
		memset(sp, 0, reinterpret_cast<uintptr_t>(top_of_stack) - reinterpret_cast<uintptr_t>(sp));
		auto frame = reinterpret_cast<stack_frame*>(sp);
		frame->ret_addr = reinterpret_cast<uintptr_t>(entry_fn);
		frame->param = reinterpret_cast<uintptr_t>(arg);
#elif defined(__arm64__) || defined(__aarch64__)
		// ARM64 requires the stack pointer to always be 16 byte aligned
		sp -= 16;
		struct stack_frame {
			uintptr_t d8_d15[8];
			uintptr_t x19_x29[11];
			union {
				uintptr_t x30;
				uintptr_t ret_addr;
			};
			union {
				uintptr_t x0;
				uintptr_t param;
			};
			uintptr_t _padding_;
		};
		static_assert(sizeof(stack_frame) == sizeof(uintptr_t) * 22);
		sp -= sizeof(stack_frame) / sizeof(uintptr_t);
		memset(sp, 0, reinterpret_cast<uintptr_t>(top_of_stack) - reinterpret_cast<uintptr_t>(sp));
		auto frame = reinterpret_cast<stack_frame*>(sp);
		frame->ret_addr = reinterpret_cast<uintptr_t>(entry_fn);
		frame->param = reinterpret_cast<uintptr_t>(arg);
#elif defined(__s390x__)
		// The s390x ABI requires a 160-byte caller allocated register save area.
		sp -= 20;
		struct stack_frame {
			uintptr_t f8_15[8];
			uintptr_t r6_13[8];
			union {
				uintptr_t r14;
				uintptr_t ret_addr;
			};
			union {
				uintptr_t r2;
				uintptr_t param;
			};
		};
		static_assert(sizeof(stack_frame) == sizeof(uintptr_t) * 18);
		sp -= sizeof(stack_frame) / sizeof(uintptr_t);
		memset(sp, 0, reinterpret_cast<uintptr_t>(top_of_stack) - reinterpret_cast<uintptr_t>(sp));
		auto frame = reinterpret_cast<stack_frame*>(sp);
		frame->ret_addr = reinterpret_cast<uintptr_t>(entry_fn);
		frame->param = reinterpret_cast<uintptr_t>(arg);
#elif defined(__powerpc64__)
		sp -= 4;
		struct stack_frame {
			uintptr_t r2;
			union {
				uintptr_t r3;
				uintptr_t param;
			};
			uintptr_t r12;
			uintptr_t r14_31[18];
			uintptr_t fr14_31[18];
			uintptr_t cr;
			uintptr_t lr;
			uintptr_t ret_addr;
		};
		static_assert(sizeof(stack_frame) == sizeof(uintptr_t) * 42);
		sp -= sizeof(stack_frame) / sizeof(uintptr_t);
		memset(sp, 0, reinterpret_cast<uintptr_t>(top_of_stack) - reinterpret_cast<uintptr_t>(sp));
		auto frame = reinterpret_cast<stack_frame*>(sp);
		// Note: On PPC a function has two entry points for local and global entry respectively
		// 		 Since we get the global entry point we also need to set the r12 register to the
		//		 same pointer.
		frame->r12 = reinterpret_cast<uintptr_t>(entry_fn);
		frame->ret_addr = reinterpret_cast<uintptr_t>(entry_fn);
		frame->param = reinterpret_cast<uintptr_t>(arg);
#else
#error "Unsupported architecture."
#endif
		ctx->current_sp = sp;
		return true;
	}

	__attribute__((naked, noinline)) inline void fiber_swap_stack(void** current_pointer_out, void* dest_pointer) {
#if defined(__i386__)
		/* `current_pointer_out` is in `4(%ebp)`. `dest_pointer` is in `8(%ebp)`. */
		//NOLINTNEXTLINE(hicpp-no-assembler)
		asm("leal -0x1c(%esp), %esp\n"
			// Save FPU state
			"stmxcsr  (%esp)\n"	   // save MMX control- and status-word
			"fnstcw   0x4(%esp)\n" // save x87 control-word
#ifdef SAVE_TLS_STACK_PROTECTOR
			"movl  %gs:0x14, %ecx\n"  // read stack guard from TLS record
			"movl  %ecx, 0x8(%esp)\n" // save stack guard
#endif
			"movl  %edi, 0xc(%esp)\n"  // save EDI
			"movl  %esi, 0x10(%esp)\n" // save ESI
			"movl  %ebx, 0x14(%esp)\n" // save EBX
			"movl  %ebp, 0x18(%esp)\n" // save EBP

			"movl 0x20(%esp), %ecx\n" // Read first arg ...
			"movl %esp, (%ecx)\n"	  // ... and copy previous stack pointer there

			"mov 0x24(%esp), %esi\n" // Read second arg ...
			"mov %esi, %esp\n"		 // ... and restore it to the stack pointer

			"ldmxcsr  (%esp)\n"	   // restore MMX control- and status-word
			"fldcw    0x4(%esp)\n" // restore x87 control-word
#ifdef SAVE_TLS_STACK_PROTECTOR
			"movl  0x8(%esp), %edx\n" // load stack guard
			"movl  %edx, %gs:0x14\n"  // restore stack guard to TLS record
#endif
			"movl 0xc(%esp), %edi\n"  // restore EDI
			"movl 0x10(%esp), %esi\n" // restore ESI
			"movl 0x14(%esp), %ebx\n" // restore EBX
			"movl 0x18(%esp), %ebp\n" // restore EBP

			"leal  0x1c(%esp), %esp\n" // Adjust stack $esp => &ret_addr

			"ret\n"); // ip = *$esp; $esp -= 4; => $esp = &_padding_;
#elif defined(__x86_64__)
#if !(defined(__LP64__) || defined(__LLP64__))
// Having non-native-sized pointers makes things very messy.
#error "Non-native pointer size."
#endif
		/* `current_pointer_out` is in `%rdi`. `dest_pointer` is in `%rsi`. */
		//NOLINTNEXTLINE(hicpp-no-assembler)
		asm("leaq -0x48(%rsp), %rsp\n"
			// Save FPU state
			"stmxcsr  (%rsp)\n"	   /* save MMX control- and status-word */
			"fnstcw   0x4(%rsp)\n" /* save x87 control-word */
#ifdef SAVE_TLS_STACK_PROTECTOR
			"movq  %fs:0x28, %rcx\n"  /* read stack guard from TLS record */
			"movq  %rcx, 0x8(%rsp)\n" /* save stack guard */
#endif
			"movq  %r12, 0x10(%rsp)\n" // save R12
			"movq  %r13, 0x18(%rsp)\n" // save R13
			"movq  %r14, 0x20(%rsp)\n" // save R14
			"movq  %r15, 0x28(%rsp)\n" // save R15
			"movq  %rbx, 0x30(%rsp)\n" // save RBX
			"movq  %rbp, 0x38(%rsp)\n" // save RBP
			// On amd64, the first argument comes from rdi.
			"movq %rsp, (%rdi)\n"
			// On amd64, the second argument comes from rsi.
			"movq %rsi, %rsp\n"
			// Restore FPU state
			"ldmxcsr (%rsp)\n"	  // Restore MMX control- and status-word
			"fldcw   0x4(%rsp)\n" // Restore x87 control-word
#ifdef SAVE_TLS_STACK_PROTECTOR
			"movq 0x8(%rsp), %rcx\n" // Restore stack guard to TLS record
			"movq %rcx, %fs:28\n"
#endif
			// Restore callee saved registers
			"movq  0x10(%rsp), %r12\n"
			"movq  0x18(%rsp), %r13\n"
			"movq  0x20(%rsp), %r14\n"
			"movq  0x28(%rsp), %r15\n"
			"movq  0x30(%rsp), %rbx\n"
			"movq  0x38(%rsp), %rbp\n"
			"movq  0x40(%rsp), %rdi\n"
			// Adjust stack ptr
			"leaq  0x48(%rsp), %rsp\n"
			// Return to the restored function
			"ret\n");
#elif defined(__arm__)
		/* `current_pointer_out` is in `r0`. `dest_pointer` is in `r1` */
		//NOLINTNEXTLINE(hicpp-no-assembler)
		asm(
			// Stack is 64bit aligned by the caller, preserve that.
			// Preserve r4-r12 and the return address (r14).
			"push {r12,r14}\n"
			"push {r4-r11}\n"
#if (defined(__VFP_FP__) && !defined(__SOFTFP__))
			"sub  sp, sp, #72\n"
			"vstmia sp, {d8-d15}\n"
#else
			"sub  sp, sp, #8\n"
#endif
			// On ARM, the first argument is in `r0`, the second argument is in `r1` and `r13` is the stack pointer.
			"str r13, [r0]\n"
			"mov r13, r1\n"
		// Restore callee save registers
#if (defined(__VFP_FP__) && !defined(__SOFTFP__))
			"vldmia  sp, {d8-d15}\n"
			"add  sp, sp, #68\n"
#else
			"sub  sp, sp, #4\n"
#endif
			"pop {r0}\n"
			"pop {r4-r11}\n"
			"pop {r12, r14}\n"
			// Return to the restored function
			"bx r14\n");
#elif defined(__arm64__) || defined(__aarch64__)
		//NOLINTNEXTLINE(hicpp-no-assembler)
		asm(
			// Preserve d8-d15 + x19-x29 and the return address (x30).
			// Note: x30 is stored twice due to alignment requirements
			"sub sp, sp, #0xb0\n"
			"stp d8,  d9,  [sp, #0x00]\n"
			"stp d10, d11, [sp, #0x10]\n"
			"stp d12, d13, [sp, #0x20]\n"
			"stp d14, d15, [sp, #0x30]\n"
			"stp x19, x20, [sp, #0x40]\n"
			"stp x21, x22, [sp, #0x50]\n"
			"stp x23, x24, [sp, #0x60]\n"
			"stp x25, x26, [sp, #0x70]\n"
			"stp x27, x28, [sp, #0x80]\n"
			"stp x29, x30, [sp, #0x90]\n"
			// On ARM64, the first argument is in `x0`. the second argument is in `x1`,
			// `sp` is the stack pointer and `x4` is a scratch register. */
			"mov x4, sp\n"
			"str x4, [x0]\n"
			"mov sp, x1\n"
			// Restore callee saved registers
			"ldp d8,  d9,  [sp, #0x00]\n"
			"ldp d10, d11, [sp, #0x10]\n"
			"ldp d12, d13, [sp, #0x20]\n"
			"ldp d14, d15, [sp, #0x30]\n"
			"ldp x19, x20, [sp, #0x40]\n"
			"ldp x21, x22, [sp, #0x50]\n"
			"ldp x23, x24, [sp, #0x60]\n"
			"ldp x25, x26, [sp, #0x70]\n"
			"ldp x27, x28, [sp, #0x80]\n"
			"ldp x29, x30, [sp, #0x90]\n"
			"ldr x0,       [sp, #0xa0]\n"
			"add sp, sp, #0xb0\n"
			// Return to the restored function
			"ret x30\n");
#elif defined(__s390x__)
		/* `current_pointer_out` is in `%r2`. `dest_pointer` is in `%r3`. */
		//NOLINTNEXTLINE(hicpp-no-assembler)
		asm(
			// Preserve r6-r13, the return address (r14), and f8-f15.
			"aghi %r15, -(8*18)\n"

			"stmg %r6, %r14, 64(%r15)\n"
			"std %f8, (8*0)(%r15)\n"
			"std %f9, (8*1)(%r15)\n"
			"std %f10, (8*2)(%r15)\n"
			"std %f11, (8*3)(%r15)\n"
			"std %f12, (8*4)(%r15)\n"
			"std %f13, (8*5)(%r15)\n"
			"std %f14, (8*6)(%r15)\n"
			"std %f15, (8*7)(%r15)\n"
			// On s390x, the first argument is in r2. r15 is the stack pointer.
			"stg %r15, 0(%r2)\n"
			// On s390x, the second argument is in r3
			"lgr %r15, %r3\n"
			// Restore callee saved registers
			"lmg %r6, %r14, (8*8)(%r15)\n"
			"ld %f8, (8*0)(%r15)\n"
			"ld %f9, (8*1)(%r15)\n"
			"ld %f10, (8*2)(%r15)\n"
			"ld %f11, (8*3)(%r15)\n"
			"ld %f12, (8*4)(%r15)\n"
			"ld %f13, (8*5)(%r15)\n"
			"ld %f14, (8*6)(%r15)\n"
			"ld %f15, (8*7)(%r15)\n"
			"lg %r2, (8*17)(%r15)\n"

			"aghi %r15, (8*18)\n"
			// Return to the restored function
			"br %r14\n");
#elif defined(__powerpc64__)
		// `current_pointer_out` is in `r3`. `dest_pointer` is in `r4`
		//NOLINTNEXTLINE(hicpp-no-assembler)
		asm("addis   %r2, %r12, .TOC.-fiber_swap_stack@ha\n"
			"addi    %r2, %r2, .TOC.-fiber_swap_stack@l\n"
			".localentry fiber_swap_stack, . - fiber_swap_stack\n"
			"addi %r1, %r1, -(42*8)\n"
			// Note: technically we only need to persist r2 and r3 on elfv1,
			// but we do it always to stay consistent
			"std %r2, (8*0)(%r1)\n"
			"std %r3, (8*1)(%r1)\n"
			"std %r12, (8*2)(%r1)\n"
			"std %r14, (8*3)(%r1)\n"
			"std %r15, (8*4)(%r1)\n"
			"std %r16, (8*5)(%r1)\n"
			"std %r17, (8*6)(%r1)\n"
			"std %r18, (8*7)(%r1)\n"
			"std %r19, (8*8)(%r1)\n"
			"std %r20, (8*9)(%r1)\n"
			"std %r21, (8*10)(%r1)\n"
			"std %r22, (8*11)(%r1)\n"
			"std %r23, (8*12)(%r1)\n"
			"std %r24, (8*13)(%r1)\n"
			"std %r25, (8*14)(%r1)\n"
			"std %r26, (8*15)(%r1)\n"
			"std %r27, (8*16)(%r1)\n"
			"std %r28, (8*17)(%r1)\n"
			"std %r29, (8*18)(%r1)\n"
			"std %r30, (8*19)(%r1)\n"
			"std %r31, (8*20)(%r1)\n"
			"stfd %f14, (8*21)(%r1)\n"
			"stfd %f15, (8*22)(%r1)\n"
			"stfd %f16, (8*23)(%r1)\n"
			"stfd %f17, (8*24)(%r1)\n"
			"stfd %f18, (8*25)(%r1)\n"
			"stfd %f19, (8*26)(%r1)\n"
			"stfd %f20, (8*27)(%r1)\n"
			"stfd %f21, (8*28)(%r1)\n"
			"stfd %f22, (8*29)(%r1)\n"
			"stfd %f23, (8*30)(%r1)\n"
			"stfd %f24, (8*31)(%r1)\n"
			"stfd %f25, (8*32)(%r1)\n"
			"stfd %f26, (8*33)(%r1)\n"
			"stfd %f27, (8*34)(%r1)\n"
			"stfd %f28, (8*35)(%r1)\n"
			"stfd %f29, (8*36)(%r1)\n"
			"stfd %f30, (8*37)(%r1)\n"
			"stfd %f31, (8*38)(%r1)\n"
			"mfcr %r0\n"
			"std %r0, (8*39)(%r1)\n"
			"mflr %r0\n"
			"std %r0, (8*40)(%r1)\n"
			"std %r0, (8*41)(%r1)\n"
			// Save old stack pointer.
			"std  %r1, 0(%r3)\n"
			// Load the new stack pointer
			"mr %r1, %r4\n"
			// Load preserved registers
			"ld %r2, (8*0)(%r1)\n"
			"ld %r3, (8*1)(%r1)\n"
			"ld %r12, (8*2)(%r1)\n"
			"ld %r14, (8*3)(%r1)\n"
			"ld %r15, (8*4)(%r1)\n"
			"ld %r16, (8*5)(%r1)\n"
			"ld %r17, (8*6)(%r1)\n"
			"ld %r18, (8*7)(%r1)\n"
			"ld %r19, (8*8)(%r1)\n"
			"ld %r20, (8*9)(%r1)\n"
			"ld %r21, (8*10)(%r1)\n"
			"ld %r22, (8*11)(%r1)\n"
			"ld %r23, (8*12)(%r1)\n"
			"ld %r24, (8*13)(%r1)\n"
			"ld %r25, (8*14)(%r1)\n"
			"ld %r26, (8*15)(%r1)\n"
			"ld %r27, (8*16)(%r1)\n"
			"ld %r28, (8*17)(%r1)\n"
			"ld %r29, (8*18)(%r1)\n"
			"ld %r30, (8*19)(%r1)\n"
			"ld %r31, (8*20)(%r1)\n"
			"lfd %f14,(8*21)(%r1)\n"
			"lfd %f15,(8*22)(%r1)\n"
			"lfd %f16,(8*23)(%r1)\n"
			"lfd %f17,(8*24)(%r1)\n"
			"lfd %f18,(8*25)(%r1)\n"
			"lfd %f19,(8*26)(%r1)\n"
			"lfd %f20,(8*27)(%r1)\n"
			"lfd %f21,(8*28)(%r1)\n"
			"lfd %f22,(8*29)(%r1)\n"
			"lfd %f23,(8*30)(%r1)\n"
			"lfd %f24,(8*31)(%r1)\n"
			"lfd %f25,(8*32)(%r1)\n"
			"lfd %f26,(8*33)(%r1)\n"
			"lfd %f27,(8*34)(%r1)\n"
			"lfd %f28,(8*35)(%r1)\n"
			"lfd %f29,(8*36)(%r1)\n"
			"lfd %f30,(8*37)(%r1)\n"
			"lfd %f31,(8*38)(%r1)\n"
			"ld %r0, (8*39)(%r1)\n"
			"mtcr %r0\n"
			"ld %r0, (8*40)(%r1)\n"
			"mtlr %r0\n"
			"ld  %r0, (8*41)(%r1)\n"
			"mtctr  %r0\n"
			"addi %r1, %r1, (8*42)\n"
			// Return to restored function
			"bctr\n");
#else
#error "Unsupported architecture."
#endif
	}

	inline bool fiber_swapcontext(fiber_context* out_ctx, fiber_context* in_ctx) {
		fiber_swap_stack(&out_ctx->current_sp, in_ctx->current_sp);
		return true;
	}

	inline bool fiber_destroy_context(fiber_context* ctx) {
		memset(ctx, 0, sizeof(fiber_context));
		return true;
	}
#endif
} // namespace asyncpp::detail
#else

#include <windows.h>

namespace asyncpp::detail {
	struct stack_context {
		// WinFiber manages the stack itself, so we only need the size
		size_t stack_size;
	};

	inline bool fiber_allocate_stack(stack_context& ctx, size_t size) noexcept {
		static size_t pagesize = []() {
			SYSTEM_INFO si;
			::GetSystemInfo(&si);
			return static_cast<size_t>(si.dwPageSize);
		}();

		// Round the stacksize to the next multiple of pages
		const auto page_count = (size + pagesize - 1) / pagesize;
		size = page_count * pagesize;
		ctx.stack_size = size;
		return true;
	}

	inline bool fiber_deallocate_stack(stack_context& ctx) noexcept { return true; }

	struct fiber_context {
		LPVOID fiber_handle;
		void (*start_fn)(void* arg);
		void* start_arg;
	};

	inline bool fiber_makecontext(fiber_context* ctx, const stack_context& stack, void (*entry_fn)(void* arg),
								  void* arg) {
		static void (*wrapper)(LPVOID) = [](LPVOID param) {
			auto ctx = static_cast<fiber_context*>(param);
			ctx->start_fn(ctx->start_arg);
		};
		ctx->fiber_handle = CreateFiber(stack.stack_size, wrapper, ctx);
		if (ctx->fiber_handle == NULL) return false;
		ctx->start_fn = entry_fn;
		ctx->start_arg = arg;
		return true;
	}

	inline bool fiber_swapcontext(fiber_context* out, fiber_context* in) {
#if (_WIN32_WINNT > 0x0600)
		if (::IsThreadAFiber() == FALSE) {
			out->fiber_handle = ::ConvertThreadToFiber(nullptr);
		} else {
			out->fiber_handle = ::GetCurrentFiber();
		}
#else
		out->fiber_handle = ::ConvertThreadToFiber(nullptr);
		if (out->fiber_handle == NULL) {
			if (::GetLastError() != ERROR_ALREADY_FIBER) return false;
			out->fiber_handle = ::GetCurrentFiber();
		}
#endif
		if (out->fiber_handle == NULL) return false;
		SwitchToFiber(in->fiber_handle);
		return true;
	}

	inline bool fiber_destroy_context(fiber_context* ctx) {
		DeleteFiber(ctx->fiber_handle);
		ctx->fiber_handle = NULL;
		return true;
	}

} // namespace asyncpp::detail
#endif
namespace asyncpp {
	template<typename TReturn>
	class fiber;
} // namespace asyncpp
namespace asyncpp::detail {
	struct fiber_handle_base {
		// C++20 coroutine ABI dictates those
		void (*resume_cb)(fiber_handle_base*) = nullptr;
		void (*destroy_cb)(fiber_handle_base*) = nullptr;

		detail::fiber_context main_context{};
		detail::fiber_context fiber_context{};
		detail::stack_context fiber_stack{};

		bool (*suspend_handler)(void* ptr, coroutine_handle<> hndl) = nullptr;
		void* suspend_handler_ptr = nullptr;
		std::exception_ptr suspend_exception = nullptr;

		coroutine_handle<> continuation{};

		bool want_destroy = false;
		bool was_started = false;
#if ASYNCPP_HAS_ASAN
		void* asan_handle = nullptr;
		const void* asan_parent_stack = nullptr;
		size_t asan_parent_stack_size{};
#endif
#if ASYNCPP_HAS_TSAN
		void* tsan_parent = nullptr;
		void* tsan_fiber = nullptr;
#endif
#if ASYNCPP_HAS_VALGRIND
		unsigned valgrind_id = 0;
#endif
	};

	inline static thread_local fiber_handle_base* g_current_fiber = nullptr;

	struct fiber_destroy_requested_exception {};

	template<typename FN>
	static coroutine_handle<> make_fiber_handle(size_t stack_size, FN&& cbfn) {
		struct handle : fiber_handle_base {
			FN function;
			explicit handle(FN&& cbfn) : function(std::forward<FN>(cbfn)) {}
		};
		static_assert(offsetof(handle, resume_cb) == 0);

		auto hndl = new handle(std::forward<FN>(cbfn));
		hndl->resume_cb = [](fiber_handle_base* hndl) {
			auto old = std::exchange(g_current_fiber, hndl);
			while (hndl->resume_cb != nullptr) {
#if ASYNCPP_HAS_ASAN
				void* asan_handle;
				__sanitizer_start_switch_fiber(&asan_handle, hndl->fiber_stack.mmap_base, hndl->fiber_stack.mmap_size);
#endif
#if ASYNCPP_HAS_TSAN
				hndl->tsan_parent = __tsan_get_current_fiber();
				__tsan_switch_to_fiber(hndl->tsan_fiber, 0);
#endif
				detail::fiber_swapcontext(&hndl->main_context, &hndl->fiber_context);
#if ASYNCPP_HAS_ASAN
				const void* fiber_stack;
				size_t fiber_stack_size;
				__sanitizer_finish_switch_fiber(asan_handle, &fiber_stack, &fiber_stack_size);
				assert(fiber_stack == hndl->fiber_stack.mmap_base);
				assert(fiber_stack_size == hndl->fiber_stack.mmap_size);
#endif
				if (hndl->suspend_handler) {
					auto handler = std::exchange(hndl->suspend_handler, nullptr);
					auto ptr = std::exchange(hndl->suspend_handler_ptr, nullptr);
					try {
						if (handler(ptr, coroutine_handle<>::from_address(static_cast<void*>(hndl)))) break;
					} catch (...) { hndl->suspend_exception = std::current_exception(); }
				}
			}
			g_current_fiber = old;
			if (hndl->resume_cb == nullptr && hndl->continuation != nullptr) { hndl->continuation.resume(); }
		};
		hndl->destroy_cb = [](fiber_handle_base* hndl) {
			// Signal destruction and resume, then delete self
			if (hndl->resume_cb != nullptr && hndl->was_started) {
				hndl->want_destroy = true;
				hndl->resume_cb(hndl);
				assert(hndl->resume_cb == nullptr);
			}
#if ASYNCPP_HAS_TSAN
			__tsan_destroy_fiber(hndl->tsan_fiber);
#endif
#if ASYNCPP_HAS_VALGRIND
			VALGRIND_STACK_DEREGISTER(hndl->valgrind_id);
#endif
			detail::fiber_destroy_context(&hndl->fiber_context);
			detail::fiber_deallocate_stack(hndl->fiber_stack);
			delete static_cast<handle*>(hndl);
		};
#if ASYNCPP_HAS_TSAN
		hndl->tsan_fiber = __tsan_create_fiber(0);
#endif
		if (!detail::fiber_allocate_stack(hndl->fiber_stack, stack_size)) {
#if ASYNCPP_HAS_TSAN
			__tsan_destroy_fiber(hndl->tsan_fiber);
#endif
			delete hndl;
			throw std::bad_alloc();
		}
		if (!detail::fiber_makecontext(
				&hndl->fiber_context, hndl->fiber_stack,
				[](void* ptr) {
					auto hndl = static_cast<handle*>(ptr);
#if ASYNCPP_HAS_ASAN
					__sanitizer_finish_switch_fiber(hndl->asan_handle, &hndl->asan_parent_stack,
													&hndl->asan_parent_stack_size);
#endif
					hndl->was_started = true;
					try {
						hndl->function();
					} catch (const fiber_destroy_requested_exception&) {} // NOLINT(bugprone-empty-catch)
					hndl->resume_cb = nullptr;
#if ASYNCPP_HAS_ASAN
					__sanitizer_start_switch_fiber(nullptr, hndl->asan_parent_stack, hndl->asan_parent_stack_size);
#endif
#if ASYNCPP_HAS_TSAN
					assert(hndl->tsan_fiber == __tsan_get_current_fiber());
					__tsan_switch_to_fiber(hndl->tsan_parent, 0);
#endif
					detail::fiber_swapcontext(&hndl->fiber_context, &hndl->main_context);
					assert(false);
				},
				hndl)) {
#if ASYNCPP_HAS_TSAN
			__tsan_destroy_fiber(hndl->tsan_fiber);
#endif
			fiber_deallocate_stack(hndl->fiber_stack);
			delete hndl;
			throw std::bad_alloc();
		}
#if ASYNCPP_HAS_VALGRIND
		hndl->valgrind_id =
			VALGRIND_STACK_REGISTER(hndl->fiber_stack.mmap_base,
									static_cast<uint8_t*>(hndl->fiber_stack.mmap_base) + hndl->fiber_stack.mmap_size);
#endif
		return coroutine_handle<>::from_address(static_cast<void*>(hndl));
	}

	template<typename T>
	static auto fiber_await(T&& awaiter) {
		// We can not suspend during exception unwinding because the unwinding library
		// uses thread local globals.
		if (std::uncaught_exceptions() != 0) std::terminate();
		if (!static_cast<bool>(awaiter.await_ready())) {
			// Awaiting is a coordination between this fiber handle's
			// resume method and the fiber itself. Because the fiber is
			// expected to be fully suspened when await_suspend is called
			// we can't directly do so here. Rather we build a callback to
			// invoke awaiter.await_suspend with the correct parameters and
			// switch_context to the resume() call. There we evaluate
			// await_suspend, passing the handle, and resume the correct handle
			// if needed. Special handling is needed for exceptions thrown in
			// await_suspend (which need to get rethrown in the fiber).
			auto hndl = detail::g_current_fiber;
			assert(hndl != nullptr);
#ifdef __linux__
			assert(__builtin_frame_address(0) > hndl->fiber_stack.mmap_base);
			assert(__builtin_frame_address(0) <
				   (static_cast<uint8_t*>(hndl->fiber_stack.mmap_base) + hndl->fiber_stack.mmap_size));
#endif
			using AwaitResult = decltype(awaiter.await_suspend(std::declval<coroutine_handle<>>()));
			hndl->suspend_handler = [](void* ptr, coroutine_handle<> hndl) {
				if constexpr (std::is_same_v<AwaitResult, void>) {
					static_cast<T*>(ptr)->await_suspend(hndl);
					return true;
				} else if constexpr (std::is_same_v<AwaitResult, bool>) {
					return static_cast<T*>(ptr)->await_suspend(hndl);
				} else { // Treat everything else as a coroutine_handle to resume
					static_cast<T*>(ptr)->await_suspend(hndl).resume();
					return true;
				}
			};
			hndl->suspend_handler_ptr = &awaiter;
#if ASYNCPP_HAS_ASAN
			void* asan_handle;
			__sanitizer_start_switch_fiber(&asan_handle, hndl->asan_parent_stack, hndl->asan_parent_stack_size);
#endif
#if ASYNCPP_HAS_TSAN
			assert(hndl->tsan_fiber == __tsan_get_current_fiber());
			__tsan_switch_to_fiber(hndl->tsan_parent, 0);
#endif
			detail::fiber_swapcontext(&hndl->fiber_context, &hndl->main_context);
#if ASYNCPP_HAS_ASAN
			__sanitizer_finish_switch_fiber(asan_handle, &hndl->asan_parent_stack, &hndl->asan_parent_stack_size);
#endif
			if (hndl->suspend_exception != nullptr)
				std::rethrow_exception(std::exchange(hndl->suspend_exception, nullptr));
			if (hndl->want_destroy) {
				// NOLINTNEXTLINE(hicpp-exception-baseclass)
				throw fiber_destroy_requested_exception{};
			}
		}
		return awaiter.await_resume();
	}

	struct fib_await_helper {
		template<typename T>
		// NOLINTNEXTLINE(misc-unconventional-assign-operator)
		auto operator=(T&& awaiter) {
			return fiber_await(std::forward<T>(awaiter));
		}
	};
} // namespace asyncpp::detail

namespace asyncpp {
	/**
	 * \brief Fiber class providing a stackfull coroutine with a `void` return value.
	 */
	template<>
	class fiber<void> {

		coroutine_handle<> m_handle;

	public:
		/**
		 * \brief Construct a new fiber for the specified entry function.
		 * 
		 * The specified function gets placed as the first function in the backtrace of the fiber and is invoked
		 * on its own stack once the fiber is awaited. Once the entry function returns a coroutine awaiting the fiber will resume.
		 * You can freely specify the stack size and it will get rounded to the next possible value after adding all overhead (guard pages).
		 * Keep in mind that most platforms treat exceeding the stack space like a segmentation fault and will terminate your program.
		 * This stack_size is a final amount, unlike the normal thread stack it does not grow automatically.
		 * \param function The function to execute when the fiber is started.
		 * \param stack_size The requested stack size in bytes. This value is rounded up to the next page size.
		 * \tparam FN 
		 */
		template<typename FN>
		explicit fiber(FN&& function, size_t stack_size = 262144)
			requires(!std::is_same_v<FN, fiber>)
			: m_handle(detail::make_fiber_handle(stack_size, std::forward<FN>(function))) {}
		/// \brief Construct an empty fiber handle
		constexpr fiber() noexcept : m_handle(nullptr) {}
		/// \brief Destructor
		~fiber() {
			if (m_handle) m_handle.destroy();
		}
		/// \brief Move constructor. The moved from handle will be empty.
		fiber(fiber&& other) noexcept : m_handle(std::exchange(other.m_handle, nullptr)) {}
		/// \brief Move constructor. The moved from handle will be empty.
		fiber& operator=(fiber&& other) noexcept {
			if (&other != this) {
				if (m_handle) m_handle.destroy();
				m_handle = std::exchange(other.m_handle, nullptr);
			}
			return *this;
		}
		fiber(const fiber&) = delete;
		fiber& operator=(const fiber&) = delete;

		/**
		 * \brief Await a standard C++20 coroutine awaitable.
		 * 
		 * Pauses the fiber till the awaited coroutine is done and returns the value generated by it.
		 * \param awaiter The awaitable to await
		 * \return The value generated by the awaitable
		 */
		template<typename T>
		static auto fiber_await(T&& awaiter) {
			return detail::fiber_await(std::forward<decltype(awaiter)>(awaiter));
		}

		/**
		 * \brief Get an awaitable for this fiber.
		 * \return auto An awaiter that is resumed once the fiber finishes
		 */
		auto await() {
			if (!m_handle) throw std::logic_error("empty fiber");
			struct awaiter {
				coroutine_handle<> m_handle;
				[[nodiscard]] bool await_ready() const noexcept { return m_handle.done(); }
				[[nodiscard]] coroutine_handle<> await_suspend(coroutine_handle<> hdl) const {
					auto ctx = static_cast<detail::fiber_handle_base*>(m_handle.address());
					if (ctx->continuation != nullptr) throw std::logic_error("already awaited");
					ctx->continuation = hdl;
					return m_handle;
				}
				constexpr void await_resume() const noexcept {}
			};
			return awaiter{m_handle};
		}

		/**
		 * \brief Get an awaitable for this fiber.
		 * \return auto An awaiter that is resumed once the fiber finishes
		 */
		auto operator co_await() { return await(); }
	};

	/**
	 * \brief Fiber class providing a stackfull coroutine.
	 */
	template<typename TReturn>
	class fiber {
		std::unique_ptr<std::optional<TReturn>> m_result{};
		fiber<void> m_base{};

	public:
		/**
		 * \brief Construct a new fiber for the specified entry function.
		 * 
		 * The specified function gets placed as the first function in the backtrace of the fiber and is invoked
		 * on its own stack once the fiber is awaited. Once the entry function returns a coroutine awaiting the fiber will resume.
		 * You can freely specify the stack size and it will get rounded to the next possible value after adding all overhead (guard pages).
		 * Keep in mind that most platforms treat exceeding the stack space like a segmentation fault and will terminate your program.
		 * This stack_size is a final amount, unlike the normal thread stack it does not grow automatically.
		 * \param function The function to execute when the fiber is started.
		 * \param stack_size The requested stack size in bytes. This value is rounded up to the next page size.
		 * \tparam FN 
		 */
		template<typename FN>
		explicit fiber(FN&& function, size_t stack_size = 262144)
			requires(!std::is_same_v<FN, fiber>)
			: m_result(std::make_unique<std::optional<TReturn>>()),
			  m_base([function = std::forward<FN>(function), res = m_result.get()]() { res->emplace(function()); },
					 stack_size) {}
		/// \brief Construct an empty fiber handle
		constexpr fiber() noexcept = default;
		/// \brief Move constructor. The moved from handle will be empty.
		fiber(fiber&& other) noexcept : m_result(std::move(other.m_result)), m_base(std::move(other.m_base)) {}
		/// \brief Move constructor. The moved from handle will be empty.
		fiber& operator=(fiber&& other) noexcept {
			if (&other != this) {
				m_base = std::move(other.m_base);
				m_result = std::move(other.m_result);
			}
			return *this;
		}
		fiber(const fiber&) = delete;
		fiber& operator=(const fiber&) = delete;

		/**
		 * \brief Await a standard C++20 coroutine awaitable.
		 * 
		 * Pauses the fiber till the awaited coroutine is done and returns the value generated by it.
		 * \param awaiter The awaitable to await
		 * \return The value generated by the awaitable
		 */
		template<typename T>
		static auto fiber_await(T&& awaiter) {
			return detail::fiber_await(std::forward<T>(awaiter));
		}

		/**
		 * \brief Get an awaitable for this fiber.
		 * \return auto An awaiter that is resumed once the fiber finishes
		 */
		auto await() & {
			if (!m_result) throw std::logic_error("empty fiber");
			struct awaiter {
				decltype(m_base.operator co_await()) m_awaiter;
				std::optional<TReturn>* m_result;
				[[nodiscard]] bool await_ready() const noexcept { return m_awaiter.await_ready(); }
				[[nodiscard]] coroutine_handle<> await_suspend(coroutine_handle<> hdl) const {
					return m_awaiter.await_suspend(hdl);
				}
				auto await_resume() const noexcept {
					assert(m_result->has_value());
					return m_result->value();
				}
			};
			return awaiter{m_base.await(), m_result.get()};
		}

		/**
		 * \brief Get an awaitable for this fiber.
		 * \return auto An awaiter that is resumed once the fiber finishes
		 */
		auto await() && {
			if (!m_result) throw std::logic_error("empty fiber");
			struct awaiter {
				decltype(m_base.operator co_await()) m_awaiter;
				std::optional<TReturn>* m_result;
				[[nodiscard]] bool await_ready() const noexcept { return m_awaiter.await_ready(); }
				[[nodiscard]] coroutine_handle<> await_suspend(coroutine_handle<> hdl) const {
					return m_awaiter.await_suspend(hdl);
				}
				auto await_resume() const noexcept {
					assert(m_result->has_value());
					return std::move(m_result->value());
				}
			};
			return awaiter{m_base.await(), m_result.get()};
		}

		/**
		 * \brief Get an awaitable for this fiber.
		 * \return auto An awaiter that is resumed once the fiber finishes
		 */
		auto operator co_await() & { return await(); }
		/**
		 * \brief Get an awaitable for this fiber.
		 * \return auto An awaiter that is resumed once the fiber finishes
		 */
		auto operator co_await() && { return await(); }
	};

	template<typename FN>
	fiber(FN&&) -> fiber<std::invoke_result_t<FN>>;
} // namespace asyncpp

#if ASYNCPP_FIBER_KEYWORDS
#define fib_await (asyncpp::detail::fib_await_helper{}) =
#endif
