#include <iostream>
#include <thread>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <future>
#include <functional>

template <typename F>
class ThreadPool {
public:
    explicit ThreadPool(size_t numThreads) :
        m_stop(false)
    {
        for (size_t i = 0; i < numThreads; ++i) {
            m_workers.emplace_back([this] { // 创建numThreads个线程
                while (true) {
                    std::function<void()> task; // 创建一个没有参数和返回值的临时函数对象
                    {
                        std::unique_lock<std::mutex> lock(m_mutex);
                        m_condition.wait(lock, [this] { return m_stop || !m_tasks.empty(); });
                        if (m_stop && m_tasks.empty()) {
                            return;
                        }
                        task = std::move(m_tasks.front());
                        m_tasks.pop();
                    }
                    task();
                }
            });
        }
    }

    ~ThreadPool() {
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_stop = true;
        }
        m_condition.notify_all();
        for (auto& worker : m_workers) {
            if (worker.joinable()) {
                worker.join();
            }
        }
    }
    /* 
    参数：一个是类型为F的函数，一个是这个函数需要的参数
    返回值：先通过declval创建一个F类型的临时函数对象，然后调用这个对象，把返回值给decltype用于类型推导，也就是把要执行的func返回值给future作为模板类型。
     */
    template <typename... Args>
    auto enqueue(F func, Args&&... args)
        -> std::future<decltype(std::declval<F>()(std::forward<Args>(args)...))>
    {
        using ReturnType = decltype(std::declval<F>()(std::forward<Args>(args)...)); // 推导func返回值类型
        auto task = std::make_shared<std::packaged_task<ReturnType()>>(   // 创建一个智能指针，指向一个packaged_task对象,这个对象包裹了一个函数对象，这个函数对象有ReturnType类型返回值，没有参数
            std::bind(std::forward<F>(func), std::forward<Args>(args)...)  // bind返回一个函数对象给packaged_task
        );
        auto future = task->get_future();
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_tasks.emplace([task]() { // 向任务队列中存储没有参数的匿名函数，这个匿名函数会调用task，task会调用func
                (*task)();
            });
        }
        m_condition.notify_one();
        return future; // 返回一个future用于主线程获取函数func的返回值
    }

private:
    std::queue<std::function<void()>> m_tasks; // 负责存储没有参数和返回值的函数对象队列
    std::mutex m_mutex;
    std::condition_variable m_condition;
    std::vector<std::thread> m_workers;
    bool m_stop;
    //F m_function;
};

int main() {
    // Example usage:
    using MyFunction = std::function<int(int, int)>;

    ThreadPool<MyFunction> pool(4); // Create a thread pool with 4 threads

    // Define a function to be executed by the thread pool
    auto task = [](int x, int y) {
        std::cout << "my task" << std::endl;
        return x * 2 * y;
    };

    std::vector<std::future<int>> futures;
    for (int i = 1; i < 4; i++) {
        for (int j = 1; j < 4; j++) {
            futures.push_back(pool.enqueue(task, i, j));
        }
    }

    for (int i = 0; i < 9; i++)
        std::cout << futures[i].get() << std::endl;

    // Enqueue tasks to the thread pool
    //std::future<int> future1 = pool.enqueue(task, 10, 20);
    //auto future2 = pool.enqueue(task, 20, 30);

    // Get the results from the futures
    //int result1 = future1.get();
    //int result2 = future2.get();
    //std::cout << "Result 1: " << result1 << std::endl;
    //std::cout << "Result 2: " << result2 << std::endl;

    return 0;
}
