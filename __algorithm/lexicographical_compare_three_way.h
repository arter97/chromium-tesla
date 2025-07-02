#ifndef LCTW_H_
#define LCTW_H_
namespace std {

/*
 * Pulled from
 * https://en.cppreference.com/w/cpp/algorithm/lexicographical_compare_three_way
 * The llvm-17 three_way code has too many trait dependencies to pull in.
 *
 *  From the FAQ: What can I do with the material on this site?
 *
 * The content is licensed under Creative Commons Attribution-Sharealike 3.0
 * Unported License (CC-BY-SA) and by the GNU Free Documentation License (GFDL)
 * (unversioned, with no invariant sections, front-cover texts, or
 * back-cover texts). That means that you can use this site in almost any way you
 * like, including mirroring, copying, translating, etc. All we would ask is to
 * provide link back to cppreference.com so that people know where to get the
 * most up-to-date content. In addition to that, any modified content should
 * be released under an equivalent license so that everyone could benefit from
 * the modified versions.
 *
 * (This code snippet is unmodified.)
 */

template<class I1, class I2, class Cmp>
constexpr auto lexicographical_compare_three_way(I1 f1, I1 l1, I2 f2, I2 l2, Cmp comp)
    -> decltype(comp(*f1, *f2))
{
    using ret_t = decltype(comp(*f1, *f2));
    static_assert(std::disjunction_v<
                      std::is_same<ret_t, std::strong_ordering>,
                      std::is_same<ret_t, std::weak_ordering>,
                      std::is_same<ret_t, std::partial_ordering>>,
                  "The return type must be a comparison category type.");

    bool exhaust1 = (f1 == l1);
    bool exhaust2 = (f2 == l2);
    for (; !exhaust1 && !exhaust2; exhaust1 = (++f1 == l1), exhaust2 = (++f2 == l2))
        if (auto c = comp(*f1, *f2); c != 0)
            return c;

    return !exhaust1 ? std::strong_ordering::greater:
           !exhaust2 ? std::strong_ordering::less:
                       std::strong_ordering::equal;
}
/* end cppreference stuff. */
}
#endif
#define LCTW_H_

