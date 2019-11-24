// original source from https://stackoverflow.com/questions/31365013/what-is-scopeguard-in-c (https://stackoverflow.com/help/licensing)
#pragma once

#include <functional>
#include <utility>

namespace libcommon {	
	class non_copyable
	{
	private:
		auto operator=(non_copyable const&)->non_copyable & = delete;
		non_copyable(non_copyable const&) = delete;
	public:
		auto operator=(non_copyable&&)->non_copyable & = default;
		non_copyable() = default;
		non_copyable(non_copyable&&) = default;
	};

	class scope_guard
		: public non_copyable
	{
	private:
		std::function<void()>    cleanup_;

	public:
		friend
			void dismiss(scope_guard& g) { g.cleanup_ = [] {}; }

		~scope_guard() { cleanup_(); }

		template< class func >
		scope_guard(func const& cleanup)
			: cleanup_(cleanup)
		{}

		scope_guard(scope_guard&& other)
			: cleanup_(std::move(other.cleanup_))
		{
			dismiss(other);
		}
	};
}
