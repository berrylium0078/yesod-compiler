#ifndef _YESOD_FRONTEND_ARENA_H_
#define _YESOD_FRONTEND_ARENA_H_

#include <cassert>
#include <cstdint>
#include <functional>
#include <stdexcept>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

namespace yesod::frontend {

template <typename T> class Arena;
template <typename T> class Handle;

namespace detail {

    template <typename T> T& currentAstGet(int32_t index);
    template <typename T> const T& currentAstGetConst(int32_t index);

} // namespace detail

template <typename T> class Handle {
  public:
    constexpr Handle() = default;
    constexpr Handle(const Handle&) = default;

    [[nodiscard]] constexpr bool isNull() const { return m_index < 0; }

    [[nodiscard]] explicit constexpr operator bool() const { return !isNull(); }

    [[nodiscard]] constexpr bool operator==(const Handle& other) const
    {
        return m_index == other.m_index;
    }

    [[nodiscard]] constexpr bool operator!=(const Handle& other) const
    {
        return !(*this == other);
    }

    [[nodiscard]] constexpr bool operator==(std::nullptr_t) const
    {
        return isNull();
    }

    [[nodiscard]] constexpr bool operator!=(std::nullptr_t) const
    {
        return !isNull();
    }

    [[nodiscard]] T& operator()(Arena<T>& arena) const { return arena[*this]; }

    [[nodiscard]] const T& operator()(const Arena<T>& arena) const
    {
        return arena[*this];
    }

    template <typename... Ts>
    [[nodiscard]] T& operator()(std::tuple<Arena<Ts>...>& arenas) const
    {
        return (*this)(std::get<Arena<T>>(arenas));
    }

    template <typename... Ts>
    [[nodiscard]] const T& operator()(
        const std::tuple<Arena<Ts>...>& arenas) const
    {
        return (*this)(std::get<Arena<T>>(arenas));
    }

    [[nodiscard]] const T* operator->() const
    {
        return &detail::currentAstGetConst<T>(m_index);
    }

    [[nodiscard]] const T& operator*() const
    {
        return detail::currentAstGetConst<T>(m_index);
    }

  private:
    friend class Arena<T>;
    template <typename U> friend U& detail::currentAstGet(int32_t index);
    template <typename U>
    friend const U& detail::currentAstGetConst(int32_t index);
    friend struct std::hash<Handle<T>>;

    explicit constexpr Handle(int32_t index)
        : m_index(index)
    {
    }

    int32_t m_index = -1;
};

template <typename T> class Arena {
  public:
    Arena() = default;

    template <typename... Args> [[nodiscard]] Handle<T> emplace(Args&&... args)
    {
        const auto index = static_cast<int32_t>(m_items.size());
        m_items.emplace_back(std::forward<Args>(args)...);
        return Handle<T>(index);
    }

    [[nodiscard]] T& operator[](Handle<T> handle)
    {
        auto index = static_cast<size_t>(handle.m_index);
        assert(index < m_items.size());
        return m_items[index];
    }

    [[nodiscard]] const T& operator[](Handle<T> handle) const
    {
        auto index = static_cast<size_t>(handle.m_index);
        assert(index < m_items.size());
        return m_items[index];
    }

    [[nodiscard]] size_t size() const { return m_items.size(); }

    [[nodiscard]] bool empty() const { return m_items.empty(); }

    void clear() { m_items.clear(); }

  private:
    std::vector<T> m_items;
};

} // namespace yesod::frontend

namespace std {

template <typename T> struct hash<yesod::frontend::Handle<T>> {
    [[nodiscard]] size_t operator()(
        const yesod::frontend::Handle<T>& handle) const noexcept
    {
        return std::hash<int32_t> {}(handle.m_index);
    }
};

} // namespace std

#endif