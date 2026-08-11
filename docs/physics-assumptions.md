# Physics assumptions

The fixture uses a horizontal worst-case pose. Load torque is `m g r`. Acceleration torque uses point-mass inertia `m r²` and a symmetric triangular profile. Motor torque is divided by gear ratio and gearbox efficiency. Available torque uses a linear torque-speed envelope; current is `I0 + torque/Kt`.

Thermal behavior uses the analytic periodic steady-cycle solution of a one-node RC network over active and rest intervals. Copper loss is approximated from movement current and winding resistance; gearbox and housing heat paths are excluded. Gearbox efficiency uses a documented bounded class range of 0.55–0.82 with 0.70 nominal. V1 reports bounded requirements and does not assign a probability distribution to this range. Center of gravity is mass-weighted. No complete center-of-gravity or tipping pass is emitted without all relevant masses and a support polygon.
