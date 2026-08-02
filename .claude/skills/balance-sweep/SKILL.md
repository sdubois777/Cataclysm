---
name: balance-sweep
description: Run the full empire-layer tuning sweeps and compare the result against a saved baseline, to see which balance conclusions a change actually moved. Use after changing the power model, the day loop, or tuning constants.
disable-model-invocation: true
---

# Run and compare a balance sweep

`sim/experiments.py` runs about 25,000 simulated campaigns and takes roughly 18
minutes. Its value is in the comparison, not in a single run, so always capture a
baseline before the change and compare afterwards.

## 1. Capture a baseline before changing anything

If the working tree already has the change in it, stash it or check out `main`
first. Comparing against a baseline produced by the modified code proves nothing.

```bash
cd sim && python -u experiments.py > baseline.txt 2>&1
```

Run it in the background and keep working. `python -u` matters: without it,
Python block-buffers when redirected and the file stays empty for minutes, which
looks like a hang.

## 2. Make the change, then run it again

```bash
cd sim && python -u experiments.py > after.txt 2>&1
```

Both runs must use the same code except for the change under test. If you edit
anything mid-run, discard the run — the sweep imports the modules once at start,
so a run straddling an edit is a mix of both versions.

## 3. Compare, being careful about what is actually comparable

The report has nine sections. Two traps:

- **Some sections use settings chosen by an earlier section.** The forge
  experiment picks the configuration with the widest margin, and the later policy
  comparison then uses whatever it picked. If that pick flips between runs, the
  later tables differ for two reasons at once. To isolate the change, compare the
  *first* policy comparison, which uses the fixed calibration settings in both.
- **Margins of one or two points are noise.** Cells are 80 to 250 campaigns. A
  recommendation that wins by a single point is not a real recommendation; say so
  rather than reporting it as a result.

Report both effects separately:

- The direct effect: what moved when settings were held identical.
- The indirect effect: whether the sweep now *recommends different numbers*. This
  is usually the consequential one, since deriving those numbers is what the rig
  is for.

## 4. Check the health metrics, not just win rates

From `sim/README.md`:

- **triage%** — free days facing two or more dungeons about to detonate. This is
  the health of the whole empire layer. Near zero means the strategy game is
  decoration, however good the win rate looks.
- **policy spread** — the gap between the best and worst way to play. If a
  careless policy scores close to a careful one, the choices are fake.
- **idle%** — free days with nothing worth doing.

A change that raises the win rate while driving triage to zero has made the game
worse. Say that plainly.

## 5. Report and file

State which conclusions changed, which held, and which are new. File a GitHub
issue for each finding that needs follow-up, self-contained enough to act on
without this conversation. Keep the two report files out of git; they are
build output, not source.
