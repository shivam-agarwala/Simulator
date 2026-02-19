#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <math.h>
#include <limits.h>

#define MAX_TASKS 10
#define MAX_JOBS_PER_TASK 500
#define MAX_JOBS (MAX_TASKS * MAX_JOBS_PER_TASK)
#define min(a, b) (((a) < (b)) ? (a) : (b))
#define max(a, b) (((a) > (b)) ? (a) : (b))

// DATA STRUCTURES & GLOBALS
#define NUM_FREQ_LEVELS 7
const double FREQ_LEVELS[] = {1.0, 0.9, 0.8, 0.7, 0.6, 0.5, 0.4};
// Note: Energy model is f * V^2 * t, with voltage levels provided.
const double VOLTAGE_LEVELS[] = {5.0, 4.7, 4.4, 4.1, 3.8, 3.5, 3.2};

typedef struct 
{
    int id;
    int phase, period, wcet, deadline;
    int invocation_times[MAX_JOBS_PER_TASK];
    int num_invocations;
    int invocation_head;
} Task;

typedef struct 
{
    int id;
    Task *task;
    int release_time;
    int absolute_deadline;
    double remaining_work;
    int actual_exec_time;
} Job;

typedef enum 
{
    PLAIN_EDF, STATIC_EDF, CCEDF, LAEDF, PLAIN_RM, STATIC_RM, CCRM
} Scheduler;

Task tasks[MAX_TASKS];
int num_tasks = 0;
double hyperperiod = 1.0;

// UTILITY & INPUT FUNCTIONS
long long gcd(long long a, long long b) 
{ 
    return b == 0 ? a : gcd(b, a % b); 
}
long long lcm(long long a, long long b) 
{
    if (a == 0 || b == 0) return 0;
    return (a * b) / gcd(a, b);
}

void calculate_hyperperiod() 
{
    if (num_tasks == 0) return;
    hyperperiod = tasks[0].period;
    for (int i = 1; i < num_tasks; i++) 
    {
        hyperperiod = lcm(hyperperiod, tasks[i].period);
    }
}

// ENERGY CALCULATION : THE CORE FORMULA (f * V^2 * t)
double calculate_energy(double duration, int freq_idx) 
{
    if (duration <= 1e-9) return 0.0;
    double voltage = VOLTAGE_LEVELS[freq_idx];
    double frequency = FREQ_LEVELS[freq_idx];
    return frequency * voltage * voltage * duration;
}

void input_tasks_from_file(const char* fn, Task task_arr[]) 
{
    FILE* f = fopen(fn, "r");
    if (!f) { perror("Cannot open tasks file"); exit(1); }
    fscanf(f, "%d", &num_tasks);
    for (int i = 0; i < num_tasks; i++) 
    {
        task_arr[i].id = i;
        fscanf(f, "%d %d %d %d", &task_arr[i].phase, &task_arr[i].period, &task_arr[i].deadline, &task_arr[i].wcet);
    }
    fclose(f);
}

void input_invocations_from_file(const char* fn, Task task_arr[]) 
{
    FILE* f = fopen(fn, "r");
    if (!f) 
    {
        printf("Warning: Could not open %s. Generating invocations for the hyperperiod.\n", fn);
        for (int i = 0; i < num_tasks; i++) 
        {
            task_arr[i].num_invocations = (int)(hyperperiod / task_arr[i].period);
            if (task_arr[i].num_invocations > MAX_JOBS_PER_TASK) 
            {
                printf("Error: Invocations needed (%d) exceeds MAX_JOBS_PER_TASK for T%d\n", task_arr[i].num_invocations, i+1);
                exit(1);
            }
            printf("   -> Auto-generating %d invocations for Task %d\n", task_arr[i].num_invocations, task_arr[i].id + 1);
            for(int j=0; j < task_arr[i].num_invocations; ++j)
            {
                 if (i == 0) task_arr[i].invocation_times[j] = (j % 2 == 0) ? 2 : 1;
                 else task_arr[i].invocation_times[j] = 1;
            }
        }
        return;
    }

    printf("\nLoaded invocation times from %s\n", fn);
    for (int i = 0; i < num_tasks; i++) 
    {
        int num_inv;
        if (fscanf(f, "%d", &num_inv) != 1) 
        {
            task_arr[i].num_invocations = 0; 
            continue;
        }
        task_arr[i].num_invocations = num_inv;
        for (int j = 0; j < num_inv; j++) 
        {
            fscanf(f, "%d", &task_arr[i].invocation_times[j]);
        }
    }
    fclose(f);
}

// FREQUENCY SELECTION LOGIC
int get_freq_idx_for_alpha(double alpha) 
{
    for (int i = NUM_FREQ_LEVELS - 1; i >= 0; i--) 
    {
        if (FREQ_LEVELS[i] >= alpha) return i;
    }
    return 0;
}

double get_static_edf_alpha() 
{
    double total_util = 0;
    for (int i = 0; i < num_tasks; i++) 
    {
        total_util += (double)tasks[i].wcet / tasks[i].period;
    }
    return total_util;
}

bool rm_schedulability_test(double alpha) 
{
    double util_sum = 0;
    for (int i = 0; i < num_tasks; i++) 
    {
        util_sum += (double)tasks[i].wcet / (alpha * tasks[i].period);
    }
    double bound = num_tasks * (pow(2.0, 1.0 / num_tasks) - 1.0);
    return util_sum <= bound;
}

double get_static_rm_alpha() 
{
    for (int i = NUM_FREQ_LEVELS - 1; i >= 0; i--) 
    {
        if (rm_schedulability_test(FREQ_LEVELS[i])) 
        return FREQ_LEVELS[i];
    }
    return 1.0;
}

double get_ccedf_alpha(double current_utils[]) 
{
    double util_sum = 0;
    for (int i = 0; i < num_tasks; i++) 
    {
        util_sum += current_utils[i];
    }
    return util_sum;
}

// LAEDF Implementation
double get_laedf_alpha(double current_time, Job* ready_queue[], int q_size) 
{
    if (q_size == 0) return FREQ_LEVELS[NUM_FREQ_LEVELS - 1]; // Lowest speed when idle

    double total_remaining_work = 0.0;
    int earliest_deadline = INT_MAX;

    for (int i = 0; i < q_size; i++) 
    {
        total_remaining_work += ready_queue[i]->remaining_work;
        if (ready_queue[i]->absolute_deadline < earliest_deadline) 
        {
            earliest_deadline = ready_queue[i]->absolute_deadline;
        }
    }
    
    // Check for negative denominator (should not happen if deadlines are met)
    if (earliest_deadline <= current_time) 
    {
        return 1.0;
    }

    double required_alpha = total_remaining_work / (earliest_deadline - current_time);
    
    //fix alpha to the range [lowest_freq, 1.0]
    required_alpha = min(required_alpha, 1.0);
    
    required_alpha = max(required_alpha, FREQ_LEVELS[NUM_FREQ_LEVELS - 1]);

    return required_alpha;
}


bool is_ccrm_schedulable(double alpha, Job* ready_queue[], int q_size) 
{
    for (int i = 0; i < num_tasks; i++) 
    {
        double response_time = tasks[i].wcet;
        while (1) 
        {
            double interference = 0;
            for (int j = 0; j < i; j++) 
            {
                interference += ceil(response_time / tasks[j].period) * tasks[j].wcet;
            }
            double my_work = 0;
            for(int k = 0; k < q_size; k++) 
            {
                if(ready_queue[k]->task->id == tasks[i].id) 
                {
                    my_work = ready_queue[k]->remaining_work; break;
                }
            }
            double new_response_time = (my_work + interference) / alpha;
            if (new_response_time > tasks[i].deadline) return false;
            if (fabs(new_response_time - response_time) < 1e-6) break;
            response_time = new_response_time;
        }
    }
    return true;
}

double get_ccrm_alpha(Job* ready_queue[], int q_size) 
{
    for (int i = NUM_FREQ_LEVELS - 1; i >= 0; i--) 
    {
        if (is_ccrm_schedulable(FREQ_LEVELS[i], ready_queue, q_size)) 
        {
            return FREQ_LEVELS[i];
        }
    }
    return 1.0;
}

// SIMULATOR CORE
double simulate(Scheduler policy, bool log_figure_data) 
{
    Job job_pool[MAX_JOBS];
    int job_count = 0;
    for (int i = 0; i < num_tasks; ++i) tasks[i].invocation_head = 0;

    Job* ready_queue[MAX_JOBS] = {NULL};
    int q_size = 0;

    double total_energy = 0;
    double current_time = 0.0;
    int decision_points = 0, preemptions = 0, context_switches = 0, deadline_misses = 0;

    double ccedf_utils[MAX_TASKS] = {0.0};
    
    double required_util = 0.0;
    double alpha = 1.0;
    int freq_idx = 0;

    const char* algo_names[] = {"Plain EDF", "Static EDF", "ccEDF", "LAEDF", "Plain RM", "Static RM", "ccRM"};
    printf("\n=== SIMULATION: %s ===\n", algo_names[policy]);
    
    if (policy == STATIC_EDF) 
    {
        required_util = get_static_edf_alpha();
        freq_idx = get_freq_idx_for_alpha(required_util);
        alpha = FREQ_LEVELS[freq_idx];
        printf("   T=0.0: Static EDF util req: %.3f -> Freq set to %.3f\n", required_util, alpha);
    } 
    else if (policy == STATIC_RM) 
    {
        required_util = get_static_rm_alpha();
        freq_idx = get_freq_idx_for_alpha(required_util);
        alpha = FREQ_LEVELS[freq_idx];
        printf("   T=0.0: Static RM schedulable at alpha=%.3f -> Freq set to %.3f\n", required_util, alpha);
    } 
    else if (policy == PLAIN_EDF || policy == PLAIN_RM) 
    {
        alpha = 1.0;
        freq_idx = 0;
        printf("   T=0.0: Freq fixed at %.3f\n", alpha);
    }
    else if (policy == LAEDF || policy == CCEDF || policy == CCRM) 
    {
        alpha = 1.0;
        freq_idx = 0;
        printf("   T=0.0: Dynamic policy initial speed: %.3f\n", alpha);
    }
    if (log_figure_data) 
    {
        printf("\n--- Log Data for ccRM ---\n");
        printf("Time\tAlpha (Speed)\n");
    }

    Job* last_job_run = NULL;

    while (current_time < hyperperiod) 
    {
        bool event_occurred = false;
        
        for (int i = 0; i < num_tasks; i++) 
        {
            if (fabs(current_time - (tasks[i].phase + round((current_time - tasks[i].phase) / tasks[i].period) * tasks[i].period)) < 1e-9 && current_time >= tasks[i].phase) 
            {
                Job* new_job = &job_pool[job_count];
                new_job->id = job_count++;
                new_job->task = &tasks[i];
                new_job->release_time = (int)current_time;
                new_job->absolute_deadline = (int)current_time + tasks[i].deadline;
                
                if (tasks[i].num_invocations > 0) 
                {
                    new_job->actual_exec_time = tasks[i].invocation_times[tasks[i].invocation_head];
                    tasks[i].invocation_head = (tasks[i].invocation_head + 1) % tasks[i].num_invocations;
                } 
                
                else 
                {
                    new_job->actual_exec_time = tasks[i].wcet;
                }
                new_job->remaining_work = new_job->actual_exec_time;

                ready_queue[q_size++] = new_job;
                decision_points++;

                printf("   T=%.1f: Released Job%d(T%d, actual=%d, deadline=%d)\n", current_time, new_job->id, tasks[i].id + 1, new_job->actual_exec_time, new_job->absolute_deadline);

                if (policy == CCEDF) 
                {
                    ccedf_utils[tasks[i].id] = (double)tasks[i].wcet / tasks[i].period;
                }
                event_occurred = true;
            }
        }
        
        double old_alpha = alpha;
        if (policy == CCEDF || policy == CCRM || policy == LAEDF) // Check for dynamic policies
        {
            if (policy == CCEDF) required_util = get_ccedf_alpha(ccedf_utils);
            if (policy == CCRM) required_util = get_ccrm_alpha(ready_queue, q_size);
            if (policy == LAEDF) required_util = get_laedf_alpha(current_time, ready_queue, q_size); // LAEDF logic
            
            freq_idx = get_freq_idx_for_alpha(required_util);
            alpha = FREQ_LEVELS[freq_idx];
            
            if (fabs(alpha - old_alpha) > 1e-9) 
            {
                printf("   T=%.1f: Freq change: %.3f -> %.3f (util req: %.3f)\n", current_time, old_alpha, alpha, required_util);
            }
        }
        if (log_figure_data && event_occurred) 
        {
             printf("%.1f\t%.3f\n", current_time, alpha);
        }

        Job* current_job = NULL;
        int best_job_idx = -1;
        if (q_size > 0) 
        {
            if (policy <= LAEDF) // Includes PLAIN_EDF, STATIC_EDF, CCEDF, LAEDF
            { // EDF-based scheduling
                int earliest_deadline = INT_MAX;
                for (int i = 0; i < q_size; i++) 
                {
                    if (ready_queue[i]->absolute_deadline < earliest_deadline) 
                    {
                        earliest_deadline = ready_queue[i]->absolute_deadline;
                        current_job = ready_queue[i];
                        best_job_idx = i;
                    }
                }
            } 
            else 
            { // RM-based scheduling
                int highest_priority = INT_MAX;
                for (int i = 0; i < q_size; i++) 
                {
                    if (ready_queue[i]->task->period < highest_priority) 
                    {
                        highest_priority = ready_queue[i]->task->period;
                        current_job = ready_queue[i];
                        best_job_idx = i;
                    }
                }
            }
        }
        
        if(current_job != last_job_run) 
        {
            context_switches++;
            if(last_job_run != NULL && current_job != NULL) preemptions++;
            if (current_job) printf("   T=%.1f: Scheduled Job%d(T%d)\n", current_time, current_job->id, current_job->task->id + 1);
        }
        last_job_run = current_job;

        double next_release_time = hyperperiod;
        for (int i = 0; i < num_tasks; i++) 
        {
            if (current_time < tasks[i].phase - 1e-9) 
            {
                next_release_time = min(next_release_time, tasks[i].phase);
            } 
            else 
            {
                 double releases_so_far = floor((current_time - tasks[i].phase + 1e-9) / tasks[i].period);
                 next_release_time = min(next_release_time, tasks[i].phase + (releases_so_far + 1) * tasks[i].period);
            }
        }
        
        double time_to_completion = hyperperiod;
        if (current_job && alpha > 1e-9) 
        {
            time_to_completion = current_time + (current_job->remaining_work / alpha);
        }
        double next_event_time = min(next_release_time, time_to_completion);
        double exec_duration = next_event_time - current_time;
        
        if(exec_duration < 1e-9) 
        {
             if (current_time >= hyperperiod) break;
             current_time += 1e-9;
             continue;
        }

        // ENERGY CALCULATION POINT 2
        if (current_job) 
        {
            total_energy += calculate_energy(exec_duration, freq_idx);
            current_job->remaining_work -= exec_duration * alpha;
        } 
        else 
        {
            printf("   T=%.1f: IDLE for %.1fms (consuming energy at lowest freq)\n", current_time, exec_duration);
            total_energy += calculate_energy(exec_duration, NUM_FREQ_LEVELS - 1);
        }

        current_time = next_event_time;

        if (current_job && current_job->remaining_work <= 1e-9) 
        {
            decision_points++;
            printf("   T=%.1f: Completed Job%d(T%d) actual=%d\n", 
                   current_time, current_job->id, current_job->task->id + 1, current_job->actual_exec_time);
            
            if (current_time > current_job->absolute_deadline + 1e-9) 
            {
                deadline_misses++;
                printf("   *** DEADLINE MISS ***\n");
            }
            
            int task_id = current_job->task->id;
            if (policy == CCEDF) 
            {
                ccedf_utils[task_id] = (double)current_job->actual_exec_time / tasks[task_id].period;
            }
            
            // Remove the completed job
            q_size--;
            if (best_job_idx < q_size) 
            {
                 ready_queue[best_job_idx] = ready_queue[q_size];
            }
            last_job_run = NULL;
        }
    }

    printf("\n--- RESULTS for %s ---\n", algo_names[policy]);
    printf("Hyperperiod: %.0f ms\n", hyperperiod);
    printf("Jobs: %d, Decision points: %d\n", job_count, decision_points);
    printf("Preemptions: %d, Context switches: %d\n", preemptions, context_switches);
    printf("Deadline misses: %d\n", deadline_misses);
    printf("Total energy: %.2f\n", total_energy);
    
    return total_energy;
}

void run_simulation_set(const char* task_file, const char* invocation_file, bool generate_fig5) 
{
    Task original_tasks[MAX_TASKS];
    
    input_tasks_from_file(task_file, original_tasks);
    for(int i=0; i<num_tasks; ++i) tasks[i] = original_tasks[i];
    calculate_hyperperiod();
    input_invocations_from_file(invocation_file, original_tasks);
    
    for(int i=0; i<num_tasks; ++i) tasks[i] = original_tasks[i];

    printf("\nTask Set Loaded: %s\n", task_file);
    for (int i = 0; i < num_tasks; i++) {
        printf("T%d: period=%d, wcet=%d (util=%.3f)\n",
               i + 1, original_tasks[i].period, original_tasks[i].wcet, (double)original_tasks[i].wcet / original_tasks[i].period);
    }
    printf("Hyperperiod: %.0f ms\n\n", hyperperiod);

    const char* names[] = {"Plain EDF", "Static EDF", "ccEDF", "LAEDF", "Plain RM", "Static RM", "ccRM"};
    double results[7];
    
    // ENERGY CALCULATION : BASELINE & NORMALIZATION
    printf("\nCalculating Baselines...\n");
    double baseline_edf = simulate(PLAIN_EDF, false);
    
    for(int i=0; i<num_tasks; ++i) tasks[i] = original_tasks[i];
    
    for(int i=0; i < num_tasks-1; i++) 
    {
        for(int j=0; j < num_tasks-i-1; j++) 
        {
            if(tasks[j].period > tasks[j+1].period) 
            {
                Task temp = tasks[j];
                tasks[j] = tasks[j+1];
                tasks[j+1] = temp;
            }
        }
    }
    double baseline_rm = simulate(PLAIN_RM, false);
    printf("\nBaselines Calculated. Running DVS simulations...\n");
    
    for(int i=0; i<num_tasks; ++i) tasks[i] = original_tasks[i];
    results[0] = 1.0;
    results[1] = simulate(STATIC_EDF, false) / baseline_edf;
    results[2] = simulate(CCEDF, false) / baseline_edf;
    results[3] = simulate(LAEDF, false) / baseline_edf;
    
    for(int i=0; i<num_tasks; ++i) tasks[i] = original_tasks[i];
    for(int i=0; i < num_tasks-1; i++) 
    {
        for(int j=0; j < num_tasks-i-1; j++) 
        {
            if(tasks[j].period > tasks[j+1].period) 
            {
                Task temp = tasks[j];
                tasks[j] = tasks[j+1];
                tasks[j+1] = temp;
            }
        }
    }
    results[4] = 1.0; 
    results[5] = simulate(STATIC_RM, false) / baseline_rm;
    results[6] = simulate(CCRM, false) / baseline_rm;

    if (generate_fig5) 
    {
       simulate(CCRM, true);
    }

    printf("\n\n--- FINAL NORMALIZED ENERGY RESULTS ---\n");
    for(int i=0; i<7; ++i)
    {
        printf("%-12s: %.3f\n", names[i], results[i]);
    }
}

// MAIN FUNCTION
int main() 
{
    if (freopen("output.txt", "w", stdout) == NULL) 
    {
        perror("Failed to redirect stdout to output.txt");
        return 1;
    }
    
    printf("REAL TIME SIMULATOR \n");
    printf("\nRUNNING SIMULATION WITH ORIGINAL TASK SET");
    run_simulation_set("tasks.txt", "invocations.txt", true);
    printf("\nRUNNING SIMULATION WITH LARGER TASK SET");
    run_simulation_set("tasks_large.txt", "invocations_large.txt", false);
    return 0;
}