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

// The RULES of the ROAD 
// If two vehicles are in the intersection simultaneously, 
// then at least ONE of the following must be true: 

// 1) V1.origin == V2.origin 
// 2) V1.origin == V2.destination && V1.destination == V2.origin 
// 3) V1.destination != V2.destination && ( V1 right turn || V2 right turn)

// To ensure these rules of the road are being followed, 
// we need the following boolean conditions: 
int volatile occupied_destinations[4]; //N:0 E:1 S:2 W:3
int volatile occupied_origins[4];      //N:0 E:1 S:2 W:3
int volatile right_turns;
int volatile intersection_vehicles;

// We need the following conditional variables 
static struct cv *new_conditions; // signal this CV every time the car leaves the intersection

// Conditional Variables and Variables
//static struct cv *unoccupied;
//bool volatile intersection_occupied; 



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

  // LOCKS
  mutex = lock_create("mutex");

  // Conditional Variables 
  new_conditions = cv_create("new conditions");
  if (new_conditions == NULL)
    panic("AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA");
  
  // Boolean setup 
  right_turns = 0;
  intersection_vehicles = 0;
  for (int i = 0; i < 3; i++)
  {
    occupied_destinations[i] = false;
    occupied_origins[i] = false;
  }
  
  // OLD CODE
  //unoccupied = cv_create("unoccupied");
  //if (unoccupied == NULL)
  //  panic("CONDITIONAL VALUE CREATION FAILED!!! ABORT ");

  return;
}

// Haoda's functions
void 
update_direction(Direction d, int* dir_array, bool set)
{
  switch (d)
    {
    case north:
      dir_array[0] = set;
      break;
    case east:
      dir_array[1] = set;
      break;
    case south:
      dir_array[2] = set;
      break;
    case west:
      dir_array[3] = set;
      break;
    }
}

int 
dir_index(Direction d)
{
  switch (d)
    {
    case north:
      return 0;
      break;
    case east:
      return 1;
      break;
    case south:
      return 2;
      break;
    case west:
      return 3;
      break;
    }
  // This should never happen 
  return 0;
}

// Haoda's DIRECTION CHECKING FUNCTIONS 
bool 
all_false_except(Direction d, int volatile arr[])
{
	int exception = dir_index(d);
	for (int i = 0; i < 4; i++)
	{
		if (i != exception && arr[i] > 0)
			return false;
	}
	return true;
}

bool 
left_and_right_all_false(Direction d, int volatile arr[])
{
	int dir_i = dir_index(d);
	for (int i = 0; i < 3; i++)
	{
		//
		if (((dir_i - i != 0) || dir_i - i != 2 || dir_i - i != 2) // If direction is not parallel
			&& arr[i] > 0) // And it's true (occupied)
			return false;
	}
	return true;
}

bool 
check_intersection(Direction origin, Direction destination)
{
	// Condition 1: Are all cars entering from the same direction?
	if (all_false_except(origin, occupied_origins))
		return true; 
	
	// Condition 1|2: Are all cars going in PARALLEL directions? 
	//               (i.e. origins and destinations are either same or opposites)
	if (left_and_right_all_false(origin, occupied_origins) && // origins are all parallel to current origin
		left_and_right_all_false(origin, occupied_destinations) && // dest are all parallel to current origin
		((dir_index(destination) - dir_index(origin) == 2) || (dir_index(destination) - dir_index(origin) == -2)))
		return true; 
	
	// Condition 3: Do all cars have different destinations? 
	if (occupied_destinations[dir_index(destination)] == false
		// and is there a car making a right turn? 
	 && (dir_index(origin) - dir_index(destination) == 1
	 || dir_index(origin) - dir_index(destination) == -3
	 //|| right_turns > 0))
	 || intersection_vehicles - right_turns <= 0)) 
	 // For one right turn to exist between every pair of vehicles, there cannot be 2 or more vehicles without a right turn
	 // On a similar note, if you are about to enter an intersection, unless you are making a right turn, 
	 // all vehicles in the intersection must be making a right turn
	{
		int diff = dir_index(origin) - dir_index(destination);
		bool isDiffDest = occupied_destinations[dir_index(destination)] == false;
		(void) diff; (void) isDiffDest;
		return true;
	}
	 // If NONE of these conditions are satisfied, then you're done m8
	 return false;
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
  KASSERT(new_conditions != NULL);
  cv_destroy(new_conditions); 
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
  lock_acquire(mutex);
  if (intersection_vehicles >= 3)
  {
	  intersection_vehicles++; // set a breakpoint like here or something 
	  intersection_vehicles--; 
  }
  lock_release(mutex); 

  /* replace this default implementation with your own implementation */
  //(void)origin;  /* avoid compiler complaint about unused parameter */
  //(void)destination; /* avoid compiler complaint about unused parameter */
  //KASSERT(intersectionSem != NULL);
  //P(intersectionSem);
  
  // HAODA's VERSION 
  //lock_acquire(mutex); // NO INTERRUPTS! 
  //  while (intersection_occupied) {
  //    cv_wait(unoccupied, mutex); // wait for it to be unoccupied
  //  }
  //  // It's unoccupied! Quick, occupy it!! 
  //  intersection_occupied = true; 
  //lock_release(mutex); // INTERRUPTS OK!
  
  // HAODA'S COMPLETE VERSION 
  lock_acquire(mutex);
  
    while (check_intersection(origin, destination) == false)
    {
      cv_wait(new_conditions, mutex);
    }
  
    occupied_destinations[dir_index(destination)]++;
    occupied_origins[dir_index(origin)]++;
    
    // Right turn stuff
    if ((dir_index(origin) - dir_index(destination) == 1
        || dir_index(origin) - dir_index(destination) == -3))
    {
      right_turns++;
      //cv_signal(new_conditions, mutex); // Signal new right turn 
    }
  
  intersection_vehicles++;
  lock_release(mutex);
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
  //(void)origin;  /* avoid compiler complaint about unused parameter */
  //(void)destination; /* avoid compiler complaint about unused parameter */
  //KASSERT(intersectionSem != NULL);
  //V(intersectionSem);
  
  lock_acquire(mutex);
    occupied_origins[dir_index(origin)]--; // unoccupy the place
	occupied_destinations[dir_index(destination)]--; 
	
	// right turn stuff 
	if ((dir_index(origin) - dir_index(destination) == 1 
		|| dir_index(origin) - dir_index(destination) == -3))
	{
		right_turns--;
    }
	
	intersection_vehicles--;
    cv_broadcast(new_conditions, mutex); // tells the CV it is unoccupied
  lock_release(mutex);
}
