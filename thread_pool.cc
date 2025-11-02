#include <chrono>   // std::chrono::seconds()
#include <functional>
#include <future>
#include <queue>
#include <stdio.h>
#include <stdlib.h> // atoi()
#include <thread>
#include <condition_variable>
#include <unistd.h> // sleep()

// thread pool

class ThreadPool
{
public:
    ThreadPool(int);
    ~ThreadPool();

    // v1
    // - background job do not return value
    // - all pending jobs are canceled when the thread pool shuts down
    void push(std::function<void()>);

    // v2
    template <typename F, typename... Args>
        std::future<typename std::result_of<F(Args...)>::type> push(
                F f, Args... args
                );

private:
    std::vector<std::thread> _workers;
    std::queue<std::function<void()>> _jobs;
    std::mutex _mtx;
    std::condition_variable _cv;
    bool _shutdown_flag{false};

    void run();
};

ThreadPool::ThreadPool(int num_threads)
{
    _workers.reserve(num_threads);
    for ( int i = 0; i < num_threads; i++ )
        _workers.emplace_back(
                [this](){ this->run(); }
                );
}

ThreadPool::~ThreadPool()
{
    printf("ThreadPool shutting down...\n");

    _shutdown_flag = true;
    _cv.notify_all();

    for ( auto& worker : _workers )
        worker.join();

    printf("# incomplete jobs %lu\n", _jobs.size());
}

void ThreadPool::push(std::function<void()> job)
{
    if ( !_shutdown_flag )
    {
        {
            std::lock_guard<std::mutex> lock(_mtx);
            _jobs.push(std::move(job));
        }
        _cv.notify_one();
    }
}

template <typename F, typename... Args>
std::future<typename std::result_of<F(Args...)>::type> ThreadPool::push(
        F f,
        Args... args)
{
    // result_of deprecated in C++17 and removed in C++20
    // using return_type = typename std::result_of<F(Args...)>::type;
    using result_t = typename std::invoke_result<F, Args...>::type;
    auto job = std::make_shared<std::packaged_task<result_t()>>(
            std::bind(f, args...)
            );
    std::future<result_t> job_future = job->get_future();

    if ( !_shutdown_flag )
    {
        {
            std::lock_guard<std::mutex> lock(_mtx);
            _jobs.push( [job]() {
                    (*job)();
                    } );
        }
        _cv.notify_one();
    }

    return job_future;
}

void ThreadPool::run()
{
    printf("worker thread start...\n");
    while ( !_shutdown_flag )
    {
        std::unique_lock<std::mutex> lock(_mtx);
        _cv.wait(lock, [this](){ return !this->_jobs.empty() || _shutdown_flag; } );

        if ( _shutdown_flag )
            return;

        std::function<void()> job = std::move(_jobs.front());
        _jobs.pop();
        lock.unlock();

        job();
    }
}

int main(int argc, char* argv[])
{
    if ( argc < 3 )
    {
        fprintf(stderr, "Usage: %s [#workers] [#jobs]\n", argv[0]);
        exit(0);
    }

    int nw = atoi(argv[1]);
    int nj = atoi(argv[2]);

    ThreadPool pool(nw);

    // simple job with no return value

    for ( int i = 1; i <= nj; i++ )
    {
        printf("push job[%d]\n", i);
        pool.push( [i]() {
                printf("job[%d] start...\n", i);
                std::this_thread::sleep_for( std::chrono::seconds(1) );
                printf("job[%d] end...\n", i);
                } );
    }

    // return values can be obtained using std::future

    std::vector<std::future<int>> futures;

    for ( int i = 1; i <= nj; i++ )
    {
        printf("push job[%d]\n", i);
        futures.push_back(
                pool.push( [](int t, int id) -> int {
                    printf("job[%d] start\n", id);
                    std::this_thread::sleep_for( std::chrono::seconds(t) );
                    printf("job[%d] end after %ds\n", id, t);
                    return id;
                    }, i % 3 + 1, i));
    }

    for ( auto& f : futures )
        printf("result: %d\n", f.get());
}
