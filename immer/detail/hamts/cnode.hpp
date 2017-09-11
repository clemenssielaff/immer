//
// immer - immutable data structures for C++
// Copyright (C) 2016, 2017 Juan Pedro Bolivar Puente
//
// This file is part of immer.
//
// immer is free software: you can redistribute it and/or modify
// it under the terms of the GNU Lesser General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// immer is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU Lesser General Public License for more details.
//
// You should have received a copy of the GNU Lesser General Public License
// along with immer.  If not, see <http://www.gnu.org/licenses/>.
//

#pragma once

#include <immer/detail/combine_standard_layout.hpp>
#include <immer/detail/util.hpp>
#include <immer/detail/hamts/bits.hpp>

#include <cassert>

#ifdef NDEBUG
#define IMMER_HAMTS_TAGGED_NODE 0
#else
#define IMMER_HAMTS_TAGGED_NODE 1
#endif

namespace immer {
namespace detail {
namespace hamts {

template <typename T,
          typename Hash,
          typename Equal,
          typename MemoryPolicy,
          bits_t B>
struct cnode
{
    using node_t      = cnode;

    using memory      = MemoryPolicy;
    using heap_policy = typename memory::heap;
    using heap        = typename heap_policy::type;
    using transience  = typename memory::transience_t;
    using refs_t      = typename memory::refcount;
    using ownee_t     = typename transience::ownee;
    using edit_t      = typename transience::edit;
    using value_t     = T;

    enum class kind_t
    {
        collision,
        inner
    };

    struct collision_t
    {
        count_t count;
        aligned_storage_for<T> buffer;
    };

    struct values_data_t
    {
        aligned_storage_for<T> buffer;
    };

    using values_t = combine_standard_layout_t<
        values_data_t, refs_t>;

    struct inner_t
    {
        bitmap_t  nodemap;
        bitmap_t  datamap;
        values_t* values;
        aligned_storage_for<node_t*> buffer;
    };

    union data_t
    {
        inner_t inner;
        collision_t collision;
    };

    struct impl_data_t
    {
#if IMMER_HAMTS_TAGGED_NODE
        kind_t kind;
#endif
        data_t data;
    };

    using impl_t = combine_standard_layout_t<
        impl_data_t, refs_t>;

    impl_t impl;

    constexpr static std::size_t sizeof_values_n(count_t count)
    {
        return immer_offsetof(values_t, d.buffer)
            + sizeof(values_data_t::buffer) * count;
    }

    constexpr static std::size_t sizeof_collision_n(count_t count)
    {
        return immer_offsetof(impl_t, d.data.collision.buffer)
            + sizeof(values_data_t::buffer) * count;
    }

    constexpr static std::size_t sizeof_inner_n(count_t count)
    {
        return immer_offsetof(impl_t, d.data.inner.buffer)
            + sizeof(inner_t::buffer) * count;
    }

#if IMMER_HAMTS_TAGGED_NODE
    kind_t kind() const
    {
        return impl.d.kind;
    }
#endif

    auto values()
    {
        assert(kind() == kind_t::inner);
        return (T*) &impl.d.data.inner.values->d.buffer;
    }

    auto children()
    {
        assert(kind() == kind_t::inner);
        return (node_t**) &impl.d.data.inner.buffer;
    }

    auto datamap() const
    {
        assert(kind() == kind_t::inner);
        return impl.d.data.inner.datamap;
    }

    auto nodemap() const
    {
        assert(kind() == kind_t::inner);
        return impl.d.data.inner.nodemap;
    }

    auto collision_count() const
    {
        assert(kind() == kind_t::collision);
        return impl.d.data.collision.count;
    }

    T* collisions()
    {
        assert(kind() == kind_t::collision);
        return (T*)&impl.d.data.collision.buffer;
    }

    static refs_t& refs(const values_t* x) { return auto_const_cast(get<refs_t>(*x)); }
    static const ownee_t& ownee(const values_t* x) { return get<ownee_t>(*x); }
    static ownee_t& ownee(values_t* x) { return get<ownee_t>(*x); }

    static refs_t& refs(const node_t* x) { return auto_const_cast(get<refs_t>(x->impl)); }
    static const ownee_t& ownee(const node_t* x) { return get<ownee_t>(x->impl); }
    static ownee_t& ownee(node_t* x) { return get<ownee_t>(x->impl); }

    static node_t* make_inner_n(count_t n)
    {
        assert(n <= branches<B>);
        auto m = heap::allocate(sizeof_inner_n(n));
        auto p = new (m) node_t;
#if IMMER_HAMTS_TAGGED_NODE
        p->impl.d.kind = node_t::kind_t::inner;
#endif
        p->impl.d.data.inner.nodemap = 0;
        p->impl.d.data.inner.datamap = 0;
        p->impl.d.data.inner.values  = nullptr;
        return p;
    }

    static node_t* make_inner_n(count_t n, values_t* values)
    {
        assert(values);
        auto p = make_inner_n(n);
        p->impl.d.data.inner.values = values;
        refs(values).inc();
        return p;
    }

    static node_t* make_inner_n(count_t n, count_t nv)
    {
        assert(nv > 0);
        assert(nv < branches<B>);
        auto p = make_inner_n(n);
        p->impl.d.data.inner.values =
            new (heap::allocate(sizeof_values_n(nv))) values_t{};
        return p;
    }

    static node_t* make_inner_n(count_t n, node_t* child)
    {
        assert(n >= 1);
        auto p = make_inner_n(n);
        p->children()[0] = child;
        return p;
    }

    static node_t* make_inner_n(count_t n,
                                count_t idx1, T x1,
                                count_t idx2, T x2)
    {
        auto p = make_inner_n(n, 2);
        p->impl.d.data.inner.datamap = (1 << idx1) | (1 << idx2);
        if (idx1 < idx2) {
            p->values()[0] = std::move(x1);
            p->values()[1] = std::move(x2);
        } else {
            assert(idx1 > idx2);
            p->values()[0] = std::move(x2);
            p->values()[1] = std::move(x1);
        }
        return p;
    }

    static node_t* make_collision_n(count_t n)
    {
        assert(n <= branches<B>);
        auto m = heap::allocate(sizeof_collision_n(n));
        auto p = new (m) node_t;
#if IMMER_HAMTS_TAGGED_NODE
        p->impl.d.kind = node_t::kind_t::collision;
#endif
        p->impl.d.data.collision.count = n;
        return p;
    }

    static node_t* make_collision(T v1, T v2)
    {
        auto m = heap::allocate(sizeof_collision_n(2));
        auto p = new (m) node_t;
        auto cols = p->collisions();
#if IMMER_HAMTS_TAGGED_NODE
        p->impl.d.kind = node_t::kind_t::collision;
#endif
        p->impl.d.data.collision.count = 2;
        cols[0] = std::move(v1);
        cols[1] = std::move(v2);
        return p;
    }

    static node_t* copy_collision_insert(node_t* src, T v)
    {
        assert(src->kind() == kind_t::collision);
        auto n    = src->collision_count();
        auto dst  = make_collision_n(n);
        auto srcp = src->collisions();
        auto dstp = dst->collisions();
        new (dstp) T{std::move(v)};
        std::uninitialized_copy(srcp, srcp + n, dstp + 1);
        return dst;
    }

    static node_t* copy_collision_replace(node_t* src, T* pos, T v)
    {
        assert(src->kind() == kind_t::collision);
        auto n    = src->collision_count();
        auto dst  = make_collision_n(n);
        auto srcp = src->collisions();
        auto dstp = dst->collisions();
        assert(pos >= srcp && pos < srcp + n);
        new (dstp) T{std::move(v)};
        dstp = std::uninitialized_copy(srcp, pos, dstp + 1);
        std::uninitialized_copy(pos + 1, srcp + n, dstp);
        return dst;
    }

    static node_t* copy_inner_replace(node_t* src,
                                      count_t offset, node_t* child)
    {
        assert(src->kind() == kind_t::inner);
        auto n    = popcount(src->nodemap());
        auto dst  = make_inner_n(n, src->impl.d.data.inner.values);
        auto srcp = src->children();
        auto dstp = dst->children();
        dst->impl.d.data.inner.datamap = src->datamap();
        dst->impl.d.data.inner.nodemap = src->nodemap();
        std::uninitialized_copy(srcp, srcp + n, dstp);
        inc_nodes(srcp, n);
        srcp[offset]->dec_unsafe();
        dstp[offset] = child;
        return dst;
    }

    static node_t* copy_inner_replace_value(node_t* src,
                                            count_t offset, T v)
    {
        assert(src->kind() == kind_t::inner);
        assert(offset < popcount(src->datamap()));
        auto n    = popcount(src->nodemap());
        auto nv   = popcount(src->datamap());
        auto dst  = make_inner_n(n, nv);
        dst->impl.d.data.inner.datamap = src->datamap();
        dst->impl.d.data.inner.nodemap = src->nodemap();
        inc_nodes(src->children(), n);
        std::uninitialized_copy(
            src->children(), src->children() + n, dst->children());
        std::uninitialized_copy(
            src->values(), src->values() + nv, dst->values());
        dst->values()[offset] = std::move(v);
        return dst;
    }

    static node_t* copy_inner_replace_merged(
        node_t* src, bitmap_t bit, count_t voffset, node_t* node)
    {
        assert(src->kind() == kind_t::inner);
        assert(!(src->nodemap() & bit));
        assert(src->datamap() & bit);
        assert(voffset == popcount(src->datamap() & (bit - 1)));
        auto n       = popcount(src->nodemap());
        auto nv      = popcount(src->datamap());
        auto dst     = make_inner_n(n + 1, nv - 1);
        auto noffset = popcount(src->nodemap() & (bit - 1));
        dst->impl.d.data.inner.datamap = src->datamap() & ~bit;
        dst->impl.d.data.inner.nodemap = src->nodemap() | bit;
        inc_nodes(src->children(), n);
        std::uninitialized_copy(
            src->children(), src->children() + noffset,
            dst->children());
        std::uninitialized_copy(
            src->children() + noffset + 1, src->children() + n,
            dst->children() + noffset + 1);
        std::uninitialized_copy(
            src->values(), src->values() + voffset,
            dst->values());
        std::uninitialized_copy(
            src->values() + noffset + 1, src->values() + nv,
            dst->values() + noffset + 1);
        dst->children()[noffset] = node;
        return dst;
    }

    static node_t* copy_inner_insert_value(node_t* src, bitmap_t bit, T v)
    {
        assert(src->kind() == kind_t::inner);
        auto n      = popcount(src->nodemap());
        auto nv     = popcount(src->datamap());
        auto offset = popcount(src->datamap() & (bit - 1));
        auto dst    = make_inner_n(n, nv + 1);
        dst->impl.d.data.inner.datamap = src->datamap() | bit;
        dst->impl.d.data.inner.nodemap = src->nodemap();
        inc_nodes(src->children(), n);
        std::uninitialized_copy(
            src->children(), src->children() + n, dst->children());
        std::uninitialized_copy(
            src->values(), src->values() + offset, dst->values());
        std::uninitialized_copy(
            src->values() + offset, src->values() + nv,
            dst->values() + offset + 1);
        dst->values()[offset] = std::move(v);
        return dst;
    }

    static node_t* make_merged(shift_t shift,
                               T v1, hash_t hash1,
                               T v2, hash_t hash2)
    {
        if (shift == max_shift<B>) {
            return make_collision(std::move(v1), std::move(v2));
        } else {
            auto idx1 = hash1 & (mask<B> << shift);
            auto idx2 = hash2 & (mask<B> << shift);
            if (idx1 == idx2) {
                return make_inner_n(1, make_merged(shift + B,
                                                   std::move(v1), hash1,
                                                   std::move(v2), hash2));
            } else {
                return make_inner_n(0,
                                    idx1 >> shift, std::move(v1),
                                    idx2 >> shift, std::move(v2));
            }
        }
    }

    node_t* inc()
    {
        refs(this).inc();
        return this;
    }

    const node_t* inc() const
    {
        refs(this).inc();
        return this;
    }

    bool dec() const { return refs(this).dec(); }
    void dec_unsafe() const { refs(this).dec_unsafe(); }

    static void inc_nodes(node_t** p, count_t n)
    {
        for (auto i = p, e = i + n; i != e; ++i)
            refs(*i).inc();
    }

    static void delete_values(values_t* p, count_t n)
    {
        assert(p);
        destroy_n(&p->d.buffer, n);
        heap::deallocate(node_t::sizeof_values_n(n), p);
    }

    static void delete_inner(node_t* p)
    {
        assert(p);
        assert(p->kind() == kind_t::inner);
        auto vp = p->impl.d.data.inner.values;
        if (vp && refs(vp).dec())
            delete_values(vp, popcount(p->datamap()));
        heap::deallocate(node_t::sizeof_inner_n(popcount(p->nodemap())), p);
    }

    static void delete_collision(node_t* p)
    {
        assert(p);
        assert(p->kind() == kind_t::collision);
        auto n = p->collision_count();
        destroy_n(p->collisions(), n);
        heap::deallocate(node_t::sizeof_collision_n(n), p);
    }
};

} // namespace hamts
} // namespace detail
} // namespace immer
