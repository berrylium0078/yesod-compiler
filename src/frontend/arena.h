#ifndef _YESOD_FRONTEND_ARENA_H_
#define _YESOD_FRONTEND_ARENA_H_

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <stdexcept>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

namespace yesod::frontend {

template <typename T> class Handle;
template <typename... Ts> class Arena;

template <typename T> class Handle {
    template <typename... Ts> friend class Arena;
    template <typename> friend class std::hash;

public:
    Handle() = default;
    Handle(const Handle&) = default;

    template <typename... Ts> auto&& operator()(Arena<Ts...>& arenas) const
    {
        return arenas[*this];
    }
    template <typename... Ts>
    auto&& operator()(const Arena<Ts...>& arenas) const
    {
        return arenas[*this];
    }
    bool operator==(const Handle<T>& other) const
    {
        return m_index == other.m_index;
    }
    bool operator!=(const Handle<T>& other) const
    {
        return m_index != other.m_index;
    }
    bool operator==(std::nullptr_t) const { return m_index < 0; }
    bool operator!=(std::nullptr_t) const { return m_index >= 0; }
    operator bool () const {
        return m_index >= 0;
    }

private:
    explicit Handle(int32_t index)
        : m_index(index)
    {
    }
    int32_t m_index = -1;
};

template <typename... Ts> class Arena {
    template <typename T> friend class Handle;

public:
    template <typename T, typename... Args> Handle<T> alloc(Args... args)
    {
        std::vector<T>& vec = std::get<std::vector<T>>(m_items);
        vec.emplace_back(std::forward<Args>(args)...);
        return Handle<T>(vec.size() - 1);
    }
    template <class Self, typename T>
    auto&& operator[](this Self&& self, Handle<T> handle)
    {
        auto&& vec = std::get<std::vector<T>>(self.m_items);
#ifdef NDEBUG
        return vec[handle.m_index];
#else
        return vec.at(handle.m_index);
#endif
    }
    void clear() {
        m_items = std::tuple<std::vector<Ts>...>{};
    }

private:
    std::tuple<std::vector<Ts>...> m_items;
};

} // namespace yesod::frontend

template <typename T> class std::hash<yesod::frontend::Handle<T>> {
public:
    std::size_t operator()(const yesod::frontend::Handle<T>& handle) const
    {
        return std::hash<int32_t>()(handle.m_index);
    }
};

#endif