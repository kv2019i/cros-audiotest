#!/usr/bin/env python3

# Copyright 2022 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
"""Tool to run cyclictest."""

import argparse
import enum
import json
import logging
import re
import subprocess
import sys
import typing

DESCRIPTION = """Run cyclictest w, w/o stress and benchmark the latency."""

CYCLICTEST_BINARY = 'cyclictest'
STRESS_BINARY = 'stress-ng'

DEFAULT_STRESS_PRIORITY = 20
DEFAULT_INTERVAL = 10000
DEFAULT_LOOPS = 6000
DEFAULT_STRESS_WORKERS = 2


class SchedPolicy(enum.Enum):
  RRSched = 'rr'  # use rr as the scheduler.
  OtherSched = 'other'  # use other(normal) as the scheduler.

  def __str__(self):
    return self.value


class Affinity(enum.Enum):
  Default = 'default'  # use all the processors in round-robin order.
  SmallCore = 'small_core'  # run all the threads on small cores.
  BigCore = 'big_core'  # run all the threads on big cores.

  def __str__(self):
    return self.value


class SchedConfig(typing.NamedTuple):
  policy: SchedPolicy  # The schedule policy.
  priority: int  # Priority of the process. If `policy` is real time, `priority` is real time priority. If `policy` is CFS, `priority` specifies the nice value.


class CyclicTestConfig(typing.NamedTuple):
  scheduler: SchedConfig  # The schedule config of the cyclictest.
  interval_us: int  # Interval time.
  threads: int  # Number of threads.
  loops: int  # Number of times.
  affinity: Affinity  # Run cyclictest threads on which sets of processors.


class StressConfig(typing.NamedTuple):
  scheduler: SchedConfig  # The schedule config of the stress process.
  workers_per_cpu: int  # Number of workers of stress per cpu.


class CyclicTestStat(typing.NamedTuple):
  min_value: int
  median: int
  p99: int
  max_value: int


def get_number_of_cpu() -> int:
  """Returns the number of cpu.

  Returns:
    Number of cpu.
  """
  lscpu = subprocess.run(
      'lscpu', stdout=subprocess.PIPE, stderr=subprocess.PIPE, encoding='utf8')
  cpu_re = '^CPU\(s\):\s*(.*)$'
  for line in lscpu.stdout.splitlines():
    match = re.fullmatch(cpu_re, line)
    if match == None:
      continue
    cpus = match.group(1)
    return int(cpus)
  logging.fatal('Failed to get number of cpu: {}'.format(lscpu.stdout))
  return -1


def get_hetero_cpu_range() -> typing.List[str]:
  """Returns a list of heterogeneous cpu ranges.

  If cores are heterogeneous ones such as ARM's, there will be more than 1 cpu
  range.
  The CPU range will be in the format of "<cpu_id>-<cpu_id>".

  Returns:
    The list of CPU range. For example: ["0-5", "6-7"].
  """

  cpuinfo = subprocess.run(['cat', '/proc/cpuinfo'],
                           stdout=subprocess.PIPE,
                           stderr=subprocess.PIPE,
                           encoding='utf8')

  # Intel does not have any 'CPU part' line. ARM does, and when it's
  # big.LITTLE, it has two different CPU parts (e.g. 0xd03 and 0xd09).
  cpu_part_re = '^CPU part\s+:\s+(0x[0-9a-f]+)$'

  previous_cpu_part = ''
  start_cpu_id = 0
  cpu_id = -1

  cpu_ranges = []
  for line in cpuinfo.stdout.splitlines():
    match = re.fullmatch(cpu_part_re, line)
    if match == None:
      continue
    cpu_part = match.group(1)
    cpu_id += 1
    if cpu_part != previous_cpu_part:
      if previous_cpu_part != '':
        cpu_ranges.append('{}-{}'.format(start_cpu_id, cpu_id - 1))
      previous_cpu_part = cpu_part
      start_cpu_id = cpu_id
  if previous_cpu_part == '':
    logging.fatal('Failed to get cpu ranges.')
    return []
  cpu_ranges.append('{}-{}'.format(start_cpu_id, cpu_id - 1))
  return cpu_ranges


class CyclicTestRunner(object):
  """Object which can set params and run cyclictest."""

  def __init__(self,
               cyclic_test_config: CyclicTestConfig,
               stress_config: typing.Optional[StressConfig] = None):
    """Initializes an CyclicTestRunner

    Args:
      cyclic_test_config: config of the `cyclictest` binary.
      stress_config: config of the `stress-ng` binary. If `stress_config` is
        None, no stress workload will be run.
    """
    self.cyclic_test_config = cyclic_test_config
    self.stress_config = stress_config

  def _get_affinity_string(self, affinity: Affinity) -> str:
    """Returns a string represents the range of CPU specified by `affinity`.

    Args:
      affinity: the specified affinity.

    Returns:
      A string represents the specified CPU range. For example, "0-5".
    """
    # TODO(eddyhsu): differientiate small/big core by cpu part info.
    cpu_ranges = get_hetero_cpu_range()
    if len(cpu_ranges) != 2:
      logging.error('Expected 2 types fo heterogeneous cores')
      return '0'
    if affinity == Affinity.SmallCore:
      return cpu_ranges[0]
    elif affinity == Affinity.BigCore:
      return cpu_ranges[1]
    logging.error('Unsupported affinity.')
    return '0'

  def _get_cyclic_test_cmd(self) -> typing.List[str]:
    """Returns the command to run `cyclictest`.

    Returns:
      A list of string represents the commands.
    """
    config = self.cyclic_test_config

    cmd = [
        CYCLICTEST_BINARY,
        '--verbose',
        # When there are multi-threads, the interval of the i-th
        # thread will be (`interval` + i * `distance`).
        # Set distance to 0 to make all the intervals equal.
        '--distance=0',
        '--policy={}'.format(str(config.scheduler.policy)),
        '--interval={}'.format(config.interval_us),
        '--threads={}'.format(config.threads),
        '--loops={}'.format(config.loops)
    ]
    if config.affinity != Affinity.Default:
      cmd += [
          '--affinity={}'.format(self._get_affinity_string(config.affinity))
      ]
    if config.scheduler.policy == SchedPolicy.RRSched:
      cmd += ['--priority={}'.format(config.scheduler.priority)]
    elif config.scheduler.policy == SchedPolicy.OtherSched:
      cmd = ['nice', '-n', str(config.scheduler.priority)] + cmd
    return cmd

  def _get_stress_cmd(self, timeout: int) -> typing.List[str]:
    """Returns the command to run stress binary.

    Returns:
      A list of string represents the commands.
    """
    config = self.stress_config
    if config is None:
      return []

    cmd = [
        STRESS_BINARY, '--timeout={}s'.format(timeout),
        '--cpu={}'.format(config.workers_per_cpu * get_number_of_cpu()),
        '--sched={}'.format(str(config.scheduler.policy))
    ]
    if config.scheduler.policy == SchedPolicy.RRSched:
      cmd += ['--sched-prio={}'.format(config.scheduler.priority)]
    elif config.scheduler.policy == SchedPolicy.OtherSched:
      cmd = ['nice', '-n', str(config.scheduler.priority)] + cmd
    return cmd

  def run(self, output_file: typing.TextIO, json_format: bool):
    """Runs the cyclictest with stress if specified and writes the results to `output_file`.

    Args:
      output_file: file to write the cyclictest results.
      json_format: write results in json format if true otherwise in human
        readable format.
    """
    # Set the timeout of stress to be 10% more of the expected time
    # of cyclic test in case the stress-ng failed to be killed.
    # `timeout` should be at least 1 otherwise `stress-ng` will run
    # forever when timeout equals 0.
    timeout = max(
        self.cyclic_test_config.loops * self.cyclic_test_config.interval_us //
        (10**6) * 11 // 10, 1)

    cyclic_test_cmd = self._get_cyclic_test_cmd()
    stress_cmd = self._get_stress_cmd(timeout)

    logging.info('Execute command: %s', ' '.join(stress_cmd))
    if self.stress_config is not None:
      # Working directory of `stress-ng` must be readable and writeable
      stress = subprocess.Popen(stress_cmd, cwd='/tmp')

    logging.info('Execute command: %s', ' '.join(cyclic_test_cmd))
    cyclictest = subprocess.run(
        cyclic_test_cmd,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        encoding='utf8')
    if cyclictest.returncode != 0:
      logging.error('Failed to execute cyclictest: {}'.format(
          cyclictest.returncode))
      logging.error('Stdout: {}'.format(cyclictest.stdout))
      logging.error('Stderr: {}'.format(cyclictest.stderr))
      return

    if self.stress_config is not None:
      if stress.wait() != 0:
        logging.error('Failed to finish stress-ng.')

    latencies = self.parse_latency(cyclictest.stdout,
                                   self.cyclic_test_config.threads)
    stats = self.calculate_stats(latencies)
    with output_file as f:
      if json_format:
        f.write(
            json.dumps({
                'stats': [{
                    'thread_id': idx,
                    'min': stat.min_value,
                    'median': stat.median,
                    'p99': stat.p99,
                    'max': stat.max_value
                } for idx, stat in enumerate(stats)]
            }))
      else:
        for idx, stat in enumerate(stats):
          f.write('Thread #{}:\n'.format(idx))
          f.write('min: {}\n'.format(stat.min_value))
          f.write('median: {}\n'.format(stat.median))
          f.write('p99: {}\n'.format(stat.p99))
          f.write('max: {}\n'.format(stat.max_value))

  def parse_latency(self, log: str,
                    threads: int) -> typing.List[typing.List[int]]:
    """Parses log of cyclictest and returns the list of latencies.

    The log will look like(task_number:count:latency_us):
    ```
    Max CPUs = 8
    Online CPUs = 8
    # /dev/cpu_dma_latency set to 0us
    Thread 0 Interval: 1000
           0:       0:       9
           0:       1:      18
           0:       2:      15
           0:       3:      14
           0:       4:      14
           0:       5:      14
           0:       6:      24
           0:       7:      16
           0:       8:      15
           0:       9:      14
    ...
    ```

    Args:
      log: string of the raw log.
      threads: number of threads cyclictest runs.

    Returns:
      A list of latencies for each thread. For example, the latencies of `i`-th
      thread will be latencies[i][:].
    """
    latencies: typing.List[typing.List[int]] = [[] for i in range(threads)]
    data_re = '^[ \t]+\d+:[ \t]+\d+:[ \t]+\d+$'
    for line in log.splitlines():
      if re.fullmatch(data_re, line) == None:
        continue
      ints = re.findall('\d+', line)
      if len(ints) != 3:
        logging.error('Failed to parse latency: {}'.format(line))
      tid = int(ints[0])
      latency = int(ints[2])
      latencies[tid].append(latency)
    return latencies

  def calculate_stats(
      self,
      latencies: typing.List[typing.List[int]]) -> typing.List[CyclicTestStat]:
    """Calculates the statistics results of latencies

    Args:
      latencies: a list of latencies for each thread

    Returns:
      A list of `CyclicTestStat` as the statistics of each thread.
    """
    stats = []
    for latency in latencies:
      sort = sorted(latency)
      num = len(latency)
      if num == 0:
        continue
      stats.append(
          CyclicTestStat(sort[0], sort[num // 2], sort[num * 99 // 100],
                         sort[-1]))
    return stats


def get_cyclictest_config(args) -> CyclicTestConfig:
  """Returns cyclictest config parsed from args.

  Args:
    args: arguments from the command line.

  Returns:
    Config of cyclictest.
  """
  return CyclicTestConfig(
      SchedConfig(args.policy, args.priority), args.interval, args.threads,
      args.loops, args.affinity)


def get_stress_config(args) -> typing.Optional[StressConfig]:
  """Returns stress config parsed from args.

  Args:
    args: arguments from the command line.

  Returns:
    Config of stress of None if stress is not specified.
  """
  if args.stress_policy is None:
    return None
  return StressConfig(
      SchedConfig(args.stress_policy, args.stress_priority),
      args.workers_per_cpu)


def main():
  parser = argparse.ArgumentParser(
      description=DESCRIPTION,
      formatter_class=argparse.RawDescriptionHelpFormatter)
  parser.add_argument(
      '--policy',
      type=SchedPolicy,
      choices=list(SchedPolicy),
      required=True,
      help='Scheduling policy of cyclictest')
  parser.add_argument(
      '--priority',
      type=int,
      required=True,
      help=('Scheduling priority of cyclictest. For realtime policy, the '
            'priority will be treated as realtime priority. For CFS, the '
            'priority will be taken as nice value.'))
  parser.add_argument(
      '--interval',
      type=int,
      default=DEFAULT_INTERVAL,
      help='Interval time for cyclictest in us.')
  parser.add_argument(
      '--threads', type=int, default=1, help='Number of threads of cyclictest.')
  parser.add_argument(
      '--loops',
      type=int,
      default=DEFAULT_LOOPS,
      help='Number of times of interval.')
  parser.add_argument(
      '--affinity',
      type=Affinity,
      choices=list(Affinity),
      default=Affinity.Default,
      help='Run cyclictest on which set of processors.')
  parser.add_argument(
      '--stress_policy',
      type=SchedPolicy,
      choices=list(SchedPolicy),
      help='Scheduling policy of stress')
  parser.add_argument(
      '--stress_priority',
      type=int,
      default=DEFAULT_STRESS_PRIORITY,
      help=('Scheduling priority of stress. For realtime policy, the '
            'priority will be treated as realtime priority. For CFS, the '
            'priority will be taken as nice value.'))
  parser.add_argument(
      '--workers_per_cpu',
      type=int,
      default=DEFAULT_STRESS_WORKERS,
      help='Number of workers per cpu for the stress')
  parser.add_argument(
      '-o',
      '--output_file',
      type=argparse.FileType('w'),
      default='-',
      help='Output file for benchmark result.')
  parser.add_argument(
      '--json',
      dest='json_format',
      action='store_true',
      help='Output in json format for easier parsing.')

  args = parser.parse_args()

  cyclic_test_config = get_cyclictest_config(args)
  stress_config = get_stress_config(args)

  runner = CyclicTestRunner(cyclic_test_config, stress_config)
  runner.run(args.output_file, args.json_format)


if __name__ == '__main__':
  main()