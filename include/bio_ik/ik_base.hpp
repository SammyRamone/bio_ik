// Copyright (c) 2016-2017, Philipp Sebastian Ruppel
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are met:
//
//    * Redistributions of source code must retain the above copyright
//      notice, this list of conditions and the following disclaimer.
//
//    * Redistributions in binary form must reproduce the above copyright
//      notice, this list of conditions and the following disclaimer in the
//      documentation and/or other materials provided with the distribution.
//
//    * Neither the name of the Universität Hamburg nor the names of its
//      contributors may be used to endorse or promote products derived from
//      this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
// AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
// IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
// ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
// LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
// CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
// SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
// INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
// CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
// ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
// POSSIBILITY OF SUCH DAMAGE.

#pragma once

#include <bio_ik/forward_kinematics.hpp>
#include <bio_ik/goal.hpp>
#include <bio_ik/problem.hpp>
#include <bio_ik/robot_info.hpp>
#include <bio_ik/utils.hpp>
#include <random>

namespace bio_ik {

struct Random {
  // std::mt19937 rng_;
  std::minstd_rand rng_;
  // std::ranlux24 rng_;
  // std::knuth_b rng_;
  // std::default_random_engine rng_;

  inline double random() {
    return std::uniform_real_distribution<double>(0, 1)(rng_);
  }

  inline std::size_t random_index(std::size_t s) {
    return std::uniform_int_distribution<size_t>(0, s - 1)(rng_);
  }

  std::normal_distribution<double> normal_distribution;
  inline double random_gauss() { return normal_distribution(rng_); }

  inline double random(double min, double max) {
    return random() * (max - min) + min;
  }

  template <class e>
  inline e& random_element(std::vector<e>& l) {
    return l[random_index(l.size())];
  }

  template <class e>
  inline const e& random_element(const std::vector<e>& l) {
    return l[random_index(l.size())];
  }

  XORShift64 _xorshift;
  inline size_t fast_random_index(size_t mod) { return _xorshift() % mod; }
  template <class T>
  inline const T& fast_random_element(const std::vector<T>& v) {
    return v[fast_random_index(v.size())];
  }

  static const size_t random_buffer_size = 1024 * 1024 * 8;

  const double* make_random_buffer() {
    static std::vector<double> buf;
    buf.resize(random_buffer_size);
    for (auto& r : buf) r = random();
    return buf.data();
  }
  const double* random_buffer;
  size_t random_buffer_index;
  inline double fast_random() {
    double r = random_buffer[random_buffer_index & (random_buffer_size - 1)];
    random_buffer_index++;
    return r;
  }

  const double* make_random_gauss_buffer() {
    // LOG("make_random_gauss_buffer");
    static std::vector<double> buf;
    buf.resize(random_buffer_size);
    for (auto& r : buf) r = random_gauss();
    return buf.data();
  }
  const double* random_gauss_buffer;
  size_t random_gauss_index;
  inline double fast_random_gauss() {
    double r =
        random_gauss_buffer[random_gauss_index & (random_buffer_size - 1)];
    random_gauss_index++;
    return r;
  }
  inline const double* fast_random_gauss_n(size_t n) {
    size_t i = random_gauss_index;
    random_gauss_index += n;
    if (random_gauss_index >= random_buffer_size) i = 0, random_gauss_index = n;
    return random_gauss_buffer + i;
  }

  Random(std::minstd_rand::result_type seed) : rng_(seed) {
    random_buffer = make_random_buffer();
    random_buffer_index = _xorshift();
    random_gauss_buffer = make_random_gauss_buffer();
    random_gauss_index = _xorshift();
  }
};

struct IKSolver : Random {
  RobotFK model_;
  RobotInfo modelInfo_;
  IKParams params_;
  size_t thread_index_;
  Problem problem_;
  std::vector<Frame> null_tip_frames_;
  volatile int canceled_;

  virtual void step() = 0;

  virtual const std::vector<double>& getSolution() const = 0;

  virtual void setParams([[maybe_unused]] const IKParams&) {}

  IKSolver(const IKParams& params)
      : Random(static_cast<long unsigned int>(params.ros_params.random_seed)),
        model_(params.robot_model),
        modelInfo_(params.robot_model),
        params_(params) {
    setParams(params);
  }
  virtual ~IKSolver() {}

  virtual void initialize(const Problem& problem) {
    problem_ = problem;
    problem_.initialize2();
    model_.initialize(problem_.tip_link_indices);
    // active_variables = problem_.active_variables;
    null_tip_frames_.resize(problem_.tip_link_indices.size());
  }

  double computeSecondaryFitnessActiveVariables(
      const double* active_variable_positions) {
    return problem_.computeGoalFitness(problem_.secondary_goals,
                                       null_tip_frames_.data(),
                                       active_variable_positions);
  }

  double computeSecondaryFitnessAllVariables(
      const std::vector<double>& variable_positions) {
    return computeSecondaryFitnessActiveVariables(
        extractActiveVariables(variable_positions));
  }

  double computeFitnessActiveVariables(
      const std::vector<Frame>& tip_frames,
      const double* active_variable_positions) {
    return problem_.computeGoalFitness(problem_.goals, tip_frames.data(),
                                       active_variable_positions);
  }

  double computeFitnessActiveVariables(
      const aligned_vector<Frame>& tip_frames,
      const double* active_variable_positions) {
    return problem_.computeGoalFitness(problem_.goals, tip_frames.data(),
                                       active_variable_positions);
  }

  double computeCombinedFitnessActiveVariables(
      const std::vector<Frame>& tip_frames,
      const double* active_variable_positions) {
    double ret = 0.0;
    ret += problem_.computeGoalFitness(problem_.goals, tip_frames.data(),
                                       active_variable_positions);
    ret += problem_.computeGoalFitness(problem_.secondary_goals,
                                       null_tip_frames_.data(),
                                       active_variable_positions);
    return ret;
  }

  double computeCombinedFitnessActiveVariables(
      const aligned_vector<Frame>& tip_frames,
      const double* active_variable_positions) {
    double ret = 0.0;
    ret += problem_.computeGoalFitness(problem_.goals, tip_frames.data(),
                                       active_variable_positions);
    ret += problem_.computeGoalFitness(problem_.secondary_goals,
                                       null_tip_frames_.data(),
                                       active_variable_positions);
    return ret;
  }

  bool checkSolutionActiveVariables(const std::vector<Frame>& tip_frames,
                                    const double* active_variable_positions) {
    return problem_.checkSolutionActiveVariables(tip_frames,
                                                 active_variable_positions);
  }

  bool checkSolution(const std::vector<double>& variable_positions,
                     const std::vector<Frame>& tips) {
    return checkSolutionActiveVariables(
        tips, extractActiveVariables(variable_positions));
  }

  std::vector<double> temp_active_variable_positions;

  double* extractActiveVariables(
      const std::vector<double>& variable_positions) {
    temp_active_variable_positions.resize(problem_.active_variables.size());
    for (size_t i = 0; i < temp_active_variable_positions.size(); i++)
      temp_active_variable_positions[i] =
          variable_positions[problem_.active_variables[i]];
    return temp_active_variable_positions.data();
  }

  double computeFitness(const std::vector<double>& variable_positions,
                        const std::vector<Frame>& tip_frames) {
    return computeFitnessActiveVariables(
        tip_frames, extractActiveVariables(variable_positions));
  }

  double computeFitness(const std::vector<double>& variable_positions) {
    model_.applyConfiguration(variable_positions);
    return computeFitness(variable_positions, model_.getTipFrames());
  }

  virtual size_t concurrency() const { return 1; }
};
}  // namespace bio_ik
