#include <chrono>   // std::chrono::seconds()
#include <functional>
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

    void push(std::function<void()>);
    void shutdown();

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

        printf("job start...\n");
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

    for ( int i = 1; i <= nj; i++ )
    {
        printf("push job[%d]\n", i);
        pool.push( [i]() {
                printf("%d: start...\n", i);
                std::this_thread::sleep_for( std::chrono::seconds(1) );
                printf("%d: end...\n", i);
                } );
    }

    sleep(2);
}
