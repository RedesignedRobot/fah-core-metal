"""make-unit.py PDB OUT: writes a small work unit shaped like a FAH explicit-solvent unit into OUT.

PDB is OpenMM's own test system wrappers/python/tests/systems/alanine-dipeptide-explicit.pdb. The unit
copies P18264's setup at small scale: Amber14 with TIP3P, PME, hydrogen mass repartitioning, a 4 fs
LangevinMiddle step, a Monte Carlo barostat and mixed precision. Needs OpenMM 8 with Python.
"""
import bz2
import sys
from pathlib import Path

import openmm
from openmm import app, unit

pdb = app.PDBFile(sys.argv[1])
out = Path(sys.argv[2])
out.mkdir(parents=True)

forcefield = app.ForceField("amber14-all.xml", "amber14/tip3p.xml")
system = forcefield.createSystem(pdb.topology, nonbondedMethod=app.PME, nonbondedCutoff=1.0 * unit.nanometer,
                                 constraints=app.HBonds, hydrogenMass=1.5 * unit.amu)
system.addForce(openmm.MonteCarloBarostat(1 * unit.bar, 300 * unit.kelvin))
# Seed 0 means a new seed for every Context, as in a real unit.
integrator = openmm.LangevinMiddleIntegrator(300 * unit.kelvin, 1 / unit.picosecond, 4 * unit.femtosecond)

# Minimize, then 10 ps of equilibration with fixed seeds so the unit can be made again.
equilibration = openmm.XmlSerializer.clone(integrator)
equilibration.setRandomNumberSeed(1)
context = openmm.Context(system, equilibration, openmm.Platform.getPlatformByName("CPU"))
context.setPositions(pdb.positions)
openmm.LocalEnergyMinimizer.minimize(context)
context.setVelocitiesToTemperature(300 * unit.kelvin, 1)
equilibration.step(2500)
state = context.getState(getPositions=True, getVelocities=True, getEnergy=True, getParameters=True)

(out / "system.xml.bz2").write_bytes(bz2.compress(openmm.XmlSerializer.serialize(system).encode()))
(out / "integrator.xml").write_text(openmm.XmlSerializer.serialize(integrator))
(out / "state.xml.bz2").write_bytes(bz2.compress(openmm.XmlSerializer.serialize(state).encode()))
# 100 ps in 1% frames, a state test against the CPU platform at every 10% checkpoint, FAHBench's limits.
(out / "core.xml").write_text("""<config>
  <numSteps v='25000'/>
  <checkpointFreq v='-10%'/>
  <xtcFreq v='250'/>
  <xtcAtoms v='solute'/>
  <precision v='mixed'/>
  <forceTolerance v='5'/>
  <energyTolerance v='10'/>
</config>
""")
print(f"{system.getNumParticles()} atoms, {pdb.topology.getNumResidues()} residues, box "
      f"{state.getPeriodicBoxVectors()[0][0].value_in_unit(unit.nanometer):.3f} nm, "
      f"PE {state.getPotentialEnergy().value_in_unit(unit.kilojoule_per_mole):.1f} kJ/mol")
