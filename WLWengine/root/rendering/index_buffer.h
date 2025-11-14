#pragma once

namespace wlw::core {
	class WIndexBuffer {
	public:
		virtual ~WIndexBuffer() = default;
		virtual void Bind() const = 0;
		virtual void Unbind() const = 0;
	};
} // wlw:core