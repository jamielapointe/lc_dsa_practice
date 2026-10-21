# Bayesian Estimation Supersedes the t-test (BEST) with ROPE

## 1. Overview and Motivation

Traditional benchmark evaluation often relies on frequentist $t$-tests or point-estimate percentage speedups. In systems performance engineering, these
approaches have fatal flaws:

- **Outlier Sensitivity**: Occasional context switches, cache misses, or background kernel interrupts create heavy-tailed latency distributions that skew normal
  distributions.
- **$p$-Value Fallacies**: A $p$-value cannot quantify the probability that a candidate is actually faster, nor can it accept the null hypothesis of
  equivalence.
- **Micro-speedup Chasing**: A candidate that is 0.1% faster with $N=10,000$ runs may yield $p < 0.001$, yet the speedup is practically meaningless and within
  thermal noise.

**BEST (Kruschke, 2013)** solves this by modeling benchmark timing data with **Student-$t$ distributions** rather than Gaussians, estimating posterior
distributions for the mean $\mu$, scale $\sigma$, and normality parameter $\nu$. Combined with a **Region of Practical Equivalence (ROPE)**, BEST yields direct,
interpretable probabilities:

- $P(\text{better})$: Probability that candidate execution time is meaningfully faster than baseline.
- $P(\text{same})$: Probability that performance difference is within the noise floor (ROPE).
- $P(\text{worse})$: Probability that candidate regressed.

---

## 2. Mathematical Model

Given baseline observations $Y_1 = \{y_{1,1}, \dots, y_{1,N_1}\}$ and candidate observations $Y_2 = \{y_{2,1}, \dots, y_{2,N_2}\}$:

$$y_{1,i} \sim \text{Student-}t(\mu_1, \sigma_1, \nu_1)$$
$$y_{2,j} \sim \text{Student-}t(\mu_2, \sigma_2, \nu_2)$$

Where:

- $\mu$: Central location parameter (mean latency in nanoseconds)
- $\sigma$: Scale parameter (spread / variability)
- $\nu \ge 1$: Normality parameter (degrees of freedom; small $\nu \approx 2-5$ accommodates heavy tails; large $\nu > 30$ approaches Gaussian)

### Difference of Means and ROPE

The core metric of interest is the difference in central tendency:

$$\Delta\mu = \mu_2 - \mu_1 = \mu_{\text{candidate}} - \mu_{\text{baseline}}$$

Since benchmark metrics measure latency or CPU time (where lower is better):

- $\Delta\mu < 0 \implies$ candidate is faster.
- $\Delta\mu > 0 \implies$ candidate is slower.

The **Region of Practical Equivalence (ROPE)** is defined as:

$$\text{ROPE} = [-\epsilon_{\text{rope}}, +\epsilon_{\text{rope}}] = [-\gamma \cdot \mu_1, +\gamma \cdot \mu_1]$$

Where $\gamma$ is typically set to $0.02$ (2%) or $0.05$ (5%).

### Posterior Decision Probabilities

From the joint posterior distribution $p(\mu_1, \mu_2 \mid Y_1, Y_2)$ sampled via MCMC or numerical quadrature / posterior approximation:

$$P(\text{better}) = \int_{-\infty}^{-\epsilon_{\text{rope}}} p(\Delta\mu \mid Y_1, Y_2) \, d\Delta\mu$$

$$P(\text{same}) = \int_{-\epsilon_{\text{rope}}}^{+\epsilon_{\text{rope}}} p(\Delta\mu \mid Y_1, Y_2) \, d\Delta\mu$$

$$P(\text{worse}) = \int_{+\epsilon_{\text{rope}}}^{+\infty} p(\Delta\mu \mid Y_1, Y_2) \, d\Delta\mu$$

By definition:
$$P(\text{better}) + P(\text{same}) + P(\text{worse}) = 1.0$$

---

## 3. Highest Density Interval (HDI) and Effect Size

### 95% Highest Density Interval (95% HDI)

The 95% HDI represents the narrowest interval containing 95% of the posterior probability mass of $\Delta\mu$.

- If the entire 95% HDI lies to the left of $-\epsilon_{\text{rope}}$, the candidate is unequivocally faster with 95%+ confidence.
- If the 95% HDI spans across zero or falls entirely inside the ROPE, the optimization is considered ineffective or equivalent.

### Standardized Effect Size (Cohen's $d$)

$$\delta = \frac{\mu_2 - \mu_1}{\sqrt{\frac{\sigma_1^2 + \sigma_2^2}{2}}}$$

- $|\delta| < 0.2$: Negligible effect
- $|\delta| \ge 0.5$: Moderate effect
- $|\delta| \ge 0.8$: Large effect
- $|\delta| \ge 1.5$: Massive architectural shift (e.g. successful SIMD vectorization)

---

## 4. Decision Rule for the Agentic Optimization Loop

| Condition | Action |
| :--- | :--- |
| $P(\text{better}) \ge 0.95$ AND Unit Tests Pass | **Promote Baseline & Commit to Git** |
| $P(\text{better}) < 0.95$ OR $P(\text{same}) > 0.50$ | **Reject Hypothesis & Clean Revert** (Insufficient speedup) |
| $P(\text{worse}) > 0.10$ | **Reject Hypothesis & Clean Revert** (Clear regression) |
| Unit Test Fails | **Reject Immediately & Clean Revert** (Correctness violation) |
