/**
 * @file FactorySets.cpp
 * @brief The factory's concerts: the table Tools/sets/make_sets.py writes (FactorySetsData.inc).
 * @author Rene Weller
 */
#include "eph/FactorySets.h"

#include <algorithm>
#include <cstring>

namespace eph
{

namespace
{

/** @brief Every concert of the factory, in the order the browser lists them. */
const FactorySet Sets[] = {
#include "FactorySetsData.inc"
};

/** @brief How many. */
constexpr int Count = static_cast<int>( sizeof( Sets ) / sizeof( Sets[0] ) );

}   // namespace

int numFactorySets()
{
    return Count;
}

const FactorySet &factorySet( int index )
{
    return Sets[std::clamp( index, 0, Count - 1 )];
}

int findFactorySet( const char *name )
{
    if ( name == nullptr ) return -1;
    for ( int i = 0; i < Count; ++i )
        if ( std::strcmp( Sets[i].m_name, name ) == 0 ) return i;
    return -1;
}

}   // namespace eph
