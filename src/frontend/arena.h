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

template <typename T, typename... Ts>
concept is_in_list = (std::same_as<T, Ts> || ...);

template <typename T> class Ptr; // might be null
template <typename T> class Ref; // can't be null
template <typename... Ts> class Arena;

template <typename T> class Ptr {
    template <typename... Ts> friend class Arena;
    template <typename> friend class std::hash;
    template <typename> friend class Ref;

public:
    Ptr() = default;
    Ptr(const Ptr&) = default;
    Ptr(const Ref<T>& other)
        : m_index(other.m_index)
    {
    }

    bool operator==(const Ptr<T>& other) const
    {
        return m_index == other.m_index;
    }
    bool operator!=(const Ptr<T>& other) const
    {
        return m_index != other.m_index;
    }
    bool operator==(std::nullptr_t) const { return m_index < 0; }
    bool operator!=(std::nullptr_t) const { return m_index >= 0; }
    operator bool() const { return m_index >= 0; }
    Ref<T> ref() const {
        if (m_index < 0) {
            throw std::runtime_error("attempted to dereference null pointer");
        }
        return Ref<T>(m_index);
    }
    template <typename... Ts>
        requires is_in_list<T, Ts...>
    T& operator()(Arena<Ts...>& arenas) const
    {
        return arenas[ref()];
    }
    template <typename... Ts>
        requires is_in_list<T, Ts...>
    const T& operator()(const Arena<Ts...>& arenas) const
    {
        return arenas[ref()];
    }

private:
    explicit Ptr(int32_t index)
        : m_index(index)
    {
    }
    int32_t m_index = -1;
};

template <typename T> class Ref {
    template <typename... Ts> friend class Arena;
    template <typename> friend class std::hash;
    template <typename> friend class Ptr;

public:
    Ref() = delete;
    Ref(const Ref&) = default;

    template <typename... Ts>
        requires is_in_list<T, Ts...>
    T& operator()(Arena<Ts...>& arenas) const
    {
        return arenas[*this];
    }
    template <typename... Ts>
        requires is_in_list<T, Ts...>
    const T& operator()(const Arena<Ts...>& arenas) const
    {
        return arenas[*this];
    }
    bool operator==(const Ref<T>& other) const
    {
        return m_index == other.m_index;
    }
    bool operator!=(const Ref<T>& other) const
    {
        return m_index != other.m_index;
    }
    bool operator==(std::nullptr_t) const { return false; }
    bool operator!=(std::nullptr_t) const { return true; }
    operator bool() const { return true; }
    Ptr<T> ptr() const {
        return Ptr<T>(m_index);
    }

private:
    explicit Ref(int32_t index)
        : m_index(index)
    {
    }
    int32_t m_index;
};

template <typename... Ts> class Arena {
    template <typename T> friend class Ptr;

public:
    template <typename T, typename... Args>
        requires is_in_list<T, Ts...> && std::is_constructible_v<T, Args...>
    Ref<T> alloc(Args... args)
    {
        std::vector<T>& vec = std::get<std::vector<T>>(m_items);
        vec.emplace_back(std::forward<Args>(args)...);
        return Ref<T>(vec.size() - 1);
    }
    template <class Self, typename T>
        requires is_in_list<T, Ts...>
    auto&& operator[](this Self&& self, Ref<T> handle)
    {
        auto&& vec = std::get<std::vector<T>>(self.m_items);
#ifdef NDEBUG
        return vec[handle.m_index];
#else
        return vec.at(handle.m_index);
#endif
    }
    void clear() { m_items = std::tuple<std::vector<Ts>...> { }; }

private:
    std::tuple<std::vector<Ts>...> m_items;
};

} // namespace yesod::frontend

template <typename T> class std::hash<yesod::frontend::Ptr<T>> {
public:
    std::size_t operator()(const yesod::frontend::Ptr<T>& handle) const
    {
        return std::hash<int32_t>()(handle.m_index);
    }
};
template <typename T> class std::hash<yesod::frontend::Ref<T>> {
public:
    std::size_t operator()(const yesod::frontend::Ref<T>& handle) const
    {
        return std::hash<int32_t>()(handle.m_index);
    }
};

#endif