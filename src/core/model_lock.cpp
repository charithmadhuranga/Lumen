// Lumen — model lock (see include/lumen/core/model_lock.hpp).
#include "lumen/core/model_lock.hpp"

namespace lumen
{

std::recursive_mutex &model_mutex()
{
    static std::recursive_mutex mutex;
    return mutex;
}

} // namespace lumen
