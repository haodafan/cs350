#include <types.h>
#include <lib.h>
#include <synchprobs.h>
#include <synch.h>
#include <opt-A1.h>

/* 
 * This simple default synchronization mechanism allows only vehicle at a time
 * into the intersection.   The intersectionSem is used as a a lock.
 * We use a semaphore rather than a lock so that this code will work even
 * before locks are implemented.
 */

/* 
 * Replace this default synchronization mechanism with your own (better) mechanism
 * needed for your solution.   Your mechanism may use any of the available synchronzation
 * primitives, e.g., semaphores, locks, condition variables.   You are also free to 
 * declare other global variables if your solution requires them.
 */

/*
 * replace this with declarations of any synchronization and other variables you need here
 */
// static struct semaphore *intersectionSem; // WE AINT GONNA USE NO SEMAPHORES IN THIS ONE 

static struct lock *mutex; 

// Conditional Variables and Variables
static struct cv *unoccupied;
bool volatile intersection_occupied; 



/* 
 * The simulation driver will call this function once before starting
 * the simulation
 *
 * You can use it to initialize synchronization and other variables.
 * 
 */
void
intersection_sync_init(void)
{
  /* replace this default implementation with your own implementation */

  //intersectionSem = sem_create("intersectionSem",1);
  //if (intersectionSem == NULL) {
  //  panic("could not create intersection semaphore");
  //}
  
  intersection_occupied = false;

  // LOCKS
  mutex = lock_create("mutex");

  unoccupied = cv_create("unoccupied");
  if (unoccupied == NULL)
    panic("CONDITIONAL VALUE CREATION FAILED!!! ABORT ");

  return;
}

/* 
 * The simulation driver will call this function once after
 * the simulation has finished
 *
 * You can use it to clean up any synchronization and other variables.
 *
 */
void
intersection_sync_cleanup(void)
{
  /* replace this default implementation with your own implementation */
  //KASSERT(intersectionSem != NULL);
  //sem_destroy(intersectionSem);
  KASSERT(unoccupied != NULL);
  cv_destroy(unoccupied); 
}


/*
 * The simulation driver will call this function each time a vehicle
 * tries to enter the intersection, before it enters.
 * This function should cause the calling simulation thread 
 * to block until it is OK for the vehicle to enter the intersection.
 *
 * parameters:
 *    * origin: the Direction from which the vehicle is arriving
 *    * destination: the Direction in which the vehicle is trying to go
 *
 * return value: none
 */

void
intersection_before_entry(Direction origin, Direction destination) 
{
  /* replace this default implementation with your own implementation */
  (void)origin;  /* avoid compiler complaint about unused parameter */
  (void)destination; /* avoid compiler complaint about unused parameter */
  //KASSERT(intersectionSem != NULL);
  //P(intersectionSem);
  
  // HAODA's VERSION 
  lock_acquire(mutex); // NO INTERRUPTS! 
    while (intersection_occupied) {
      cv_wait(unoccupied, mutex); // wait for it to be unoccupied
    }
    // It's unoccupied! Quick, occupy it!! 
    intersection_occupied = true; 
  lock_release(mutex); // INTERRUPTS OK!
}


/*
 * The simulation driver will call this function each time a vehicle
 * leaves the intersection.
 *
 * parameters:
 *    * origin: the Direction from which the vehicle arrived
 *    * destination: the Direction in which the vehicle is going
 *
 * return value: none
 */

void
intersection_after_exit(Direction origin, Direction destination) 
{
  /* replace this default implementation with your own implementation */
  (void)origin;  /* avoid compiler complaint about unused parameter */
  (void)destination; /* avoid compiler complaint about unused parameter */
  //KASSERT(intersectionSem != NULL);
  //V(intersectionSem);
  
  lock_acquire(mutex);
    intersection_occupied = false; // unoccupy the place
    cv_signal(unoccupied, mutex); // tells the CV it is unoccupied
  lock_release(mutex);
}
