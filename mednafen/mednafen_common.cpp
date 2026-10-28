#include <mednafen/mednafen.h>
#include <mednafen/MThreading.h>
#include <mednafen/Time.h>
#include <pthread.h>
#include <semaphore.h>
#ifdef __APPLE__
#include <dispatch/dispatch.h>
#endif
#include <time.h>
#include <unistd.h>

namespace Mednafen
{
MDFN_Error::MDFN_Error(int errno_code_new, const char* format, ...) noexcept : errno_code(errno_code_new), error_message(nullptr)
{
 va_list ap;
 va_start(ap, format);
 vfprintf(stderr, format, ap);
 va_end(ap);
 fputc('\n', stderr);
 abort();
}

MDFN_Error::~MDFN_Error() noexcept { }

const char* MDFN_Error::what(void) const noexcept { return "satz mednafen error"; }

bool MDFNSS_StateAction(StateMem*, const unsigned, const bool, const SFORMAT*, const char*, const bool) noexcept { return true; }

namespace Time
{
void SleepMS(uint32 amount) noexcept { usleep(amount * 1000); }
}

namespace MThreading
{
struct Thread { pthread_t thread; int (*entry)(void*); void* data; int result; };
#ifdef __APPLE__
struct Sem { dispatch_semaphore_t sem; };
#else
struct Sem { sem_t sem; };
#endif

static void* thread_start(void* arg)
{
 Thread* t = (Thread*)arg;
 t->result = t->entry(t->data);
 return nullptr;
}

Thread* Thread_Create(int (*fn)(void*), void* data, const char*)
{
 Thread* t = new Thread();
 t->entry = fn;
 t->data = data;
 pthread_create(&t->thread, nullptr, thread_start, t);
 return t;
}

void Thread_Wait(Thread* thread, int* status)
{
 pthread_join(thread->thread, nullptr);
 if(status)
  *status = thread->result;
 delete thread;
}

uint64 Thread_SetAffinity(Thread*, uint64) { return 0; }

#ifdef __APPLE__
Sem* Sem_Create(void)
{
 Sem* s = new Sem();
 s->sem = dispatch_semaphore_create(0);
 return s;
}

void Sem_Destroy(Sem* sem) noexcept
{
 dispatch_release(sem->sem);
 delete sem;
}

bool Sem_Post(Sem* sem) noexcept { dispatch_semaphore_signal(sem->sem); return true; }

bool Sem_TimedWait(Sem* sem, unsigned ms) noexcept
{
 return dispatch_semaphore_wait(sem->sem, dispatch_time(DISPATCH_TIME_NOW, (int64_t)ms * 1000000)) == 0;
}
#else
Sem* Sem_Create(void)
{
 Sem* s = new Sem();
 sem_init(&s->sem, 0, 0);
 return s;
}

void Sem_Destroy(Sem* sem) noexcept
{
 sem_destroy(&sem->sem);
 delete sem;
}

bool Sem_Post(Sem* sem) noexcept { return sem_post(&sem->sem) == 0; }

bool Sem_TimedWait(Sem* sem, unsigned ms) noexcept
{
 struct timespec ts;
 clock_gettime(CLOCK_REALTIME, &ts);
 ts.tv_nsec += (long)(ms % 1000) * 1000000;
 ts.tv_sec += ms / 1000 + ts.tv_nsec / 1000000000;
 ts.tv_nsec %= 1000000000;
 return sem_timedwait(&sem->sem, &ts) == 0;
}
#endif
}
}
