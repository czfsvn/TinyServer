#include <functional>

namespace cncpp
{
    class DeferFunctor
    {
    private:
        std::function<void()> func_;

    public:
        DeferFunctor(const std::function<void()>& func) : func_(func)
        {
        }

        ~DeferFunctor()
        {
            func_();
        }
    };
}  // namespace cncpp