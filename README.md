# Simulator

# Real-Time Scheduling Simulator with Energy-Aware DVFS (C)

A complete **real time scheduling simulator** built in C that evaluates multiple fixed and dynamic priority scheduling algorithms under **energy aware Dynamic Voltage and Frequency Scaling (DVFS)**.

This project simulates periodic real time task systems, generates job executions over the hyperperiod, and compares scheduling strategies based on:

* Deadline satisfaction
* Execution timeline
* CPU frequency selection
* Energy consumption

---

## Project Motivation

Real-time embedded and cyber-physical systems (automotive ECUs, IoT devices, avionics, robotics) must:

* Meet strict deadlines
* Minimize energy consumption
* Efficiently utilize CPU resources

This simulator helps analyze how different **EDF/RM variants + DVFS techniques** perform in such environments.

---

## Features

### Supported Scheduling Algorithms

| Category         | Algorithm      | Description                  |
| ---------------- | -------------- | ---------------------------- |
| Dynamic Priority | **Plain EDF**  | Earliest Deadline First      |
| Dynamic Priority | **Static EDF** | EDF with static DVFS scaling |
| Dynamic Priority | **CCEDF**      | Cycle Conserving EDF         |
| Dynamic Priority | **LAEDF**      | Look Ahead EDF               |
| Fixed Priority   | **Plain RM**   | Rate Monotonic               |
| Fixed Priority   | **Static RM**  | RM with static DVFS          |
| Fixed Priority   | **CCRM**       | Cycle Conserving RM          |

---

### Energy Aware DVFS Model

The simulator integrates a realistic CPU energy model:

[
Energy = f \times V^2 \times t
]

Where:

* `f` → CPU frequency
* `V` → Voltage
* `t` → Execution duration

#### Supported Frequency Levels

| Level | Frequency | Voltage |
| ----- | --------- | ------- |
| 1     | 1.0       | 5.0 V   |
| 2     | 0.9       | 4.7 V   |
| 3     | 0.8       | 4.4 V   |
| 4     | 0.7       | 4.1 V   |
| 5     | 0.6       | 3.8 V   |
| 6     | 0.5       | 3.5 V   |
| 7     | 0.4       | 3.2 V   |

---

## Simulation Overview

The simulator performs the following steps:

1. Reads periodic task set
2. Computes **hyperperiod (LCM of periods)**
3. Generates job invocations
4. Simulates scheduling timeline
5. Dynamically selects CPU frequency
6. Calculates total energy consumption
7. Produces execution trace + results

---

## Repository Structure

```
.
├── simulator.c          # Main simulator source code
├── tasks.txt            # Small task set input
├── tasks_large.txt      # Large task set input
├── invocations.txt      # Job execution times
├── invocations_large.txt
├── output.txt           # Sample simulation output
```

---

## Input File Format

### tasks.txt

Format:

```
<num_tasks>
<phase> <period> <deadline> <WCET>
<phase> <period> <deadline> <WCET>
...
```

Example:

```
3
0 10 10 2
0 15 15 3
0 20 20 5
```

Field meanings:

| Field    | Description               |
| -------- | ------------------------- |
| Phase    | First release time        |
| Period   | Task period               |
| Deadline | Relative deadline         |
| WCET     | Worst-Case Execution Time |

---

### invocations.txt

Defines actual execution time of each job instance.

Format per task:

```
<num_invocations>
<exec_time_1> <exec_time_2> ...
```

If this file is **missing**, the simulator automatically generates invocation times.

---

## How to Compile & Run

### Step 1 — Compile

```bash
gcc simulator.c -o simulator -lm
```

---

### Step 2 — Run Simulation

```bash
./simulator tasks.txt invocations.txt > output.txt
```

Or test large dataset:

```bash
./simulator tasks_large.txt invocations_large.txt > output.txt
```

---

## Output

The simulator prints:

* Scheduling timeline
* Selected CPU frequency per job
* Deadline misses (if any)
* Total energy consumption
* Algorithm comparison

Example output snippet:

```
Loaded invocation times from invocations.txt
Hyperperiod = 60

Running PLAIN EDF...
Total Energy Consumed = 1234.56 units
No Deadline Misses ✔
```

---

## Learning Outcomes

This project demonstrates:

* Real time scheduling theory in practice
* EDF vs RM trade offs
* DVFS integration in scheduling
* Energy aware system design
* Systems programming in C

---

## Possible Extensions

Future improvements could include:

* Sporadic / aperiodic tasks
* Multi core scheduling
* Thermal aware scheduling
* Visualization dashboard
* Statistical performance graphs

---

## Author

**Shivam Agarwala**
Operating Systems & Real Time Scheduling Project

---
