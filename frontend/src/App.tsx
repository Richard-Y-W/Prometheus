import {useState} from 'react';
import {Canvas} from '@react-three/fiber';
import {Bounds, Grid, OrbitControls} from '@react-three/drei';

import {api} from './api';
import {disabledActionExplanation} from './logic';
import type {Finding, Part} from './types';

const fixture: Part = {
  id: 'assembly',
  name: 'Motor-driven arm',
  type: 'assembly',
  badge: '',
  shape: 'box',
  position: [0, 0, 0],
  scale: [1, 1, 1],
  children: [
    {id: 'base', name: 'Base plate', type: 'part', badge: 'Geometry Only', shape: 'box', position: [0, 0, 0], scale: [3.2, 0.3, 2.2]},
    {id: 'motor', name: 'Motor placeholder', type: 'part', badge: 'Geometry Only', shape: 'cylinder', position: [0, 0.55, 0], scale: [0.65, 1, 0.65]},
    {id: 'arm', name: 'Arm', type: 'part', badge: 'Geometry Only', shape: 'box', position: [1.55, 0.55, 0], scale: [2.6, 0.24, 0.35]},
    {id: 'payload', name: '8 kg payload', type: 'payload', badge: 'Envelope Model', shape: 'box', position: [3, 0.55, 0], scale: [0.65, 0.8, 0.65]},
  ],
};

function ArchiveBanner() {
  return (
    <div className="archive-banner">
      Archived rough-V1 interface — engineering execution is disabled while the reviewed C++ path is rebuilt.
    </div>
  );
}

type PartMeshProps = {
  part: Part;
  selected: boolean;
  onSelect: (id: string) => void;
  problem: boolean;
};

function PartMesh({part, selected, onSelect, problem}: PartMeshProps) {
  return (
    <mesh
      position={part.position as [number, number, number]}
      scale={part.scale as [number, number, number]}
      rotation={part.shape === 'cylinder' ? [Math.PI / 2, 0, 0] : [0, 0, 0]}
      onClick={(event) => {
        event.stopPropagation();
        onSelect(part.id);
      }}
      castShadow
      receiveShadow
    >
      {part.shape === 'box' ? (
        <boxGeometry args={[1, 1, 1]} />
      ) : (
        <cylinderGeometry args={[0.5, 0.5, 1, 32]} />
      )}
      <meshStandardMaterial
        color={problem ? '#c75050' : selected ? '#54a8e8' : part.id === 'arm' ? '#8997a5' : '#596572'}
        emissive={selected ? '#183b56' : '#000000'}
        roughness={0.55}
      />
    </mesh>
  );
}

type ViewportProps = {
  selected: string;
  setSelected: (id: string) => void;
  findings: Finding[];
};

function Viewport({selected, setSelected, findings}: ViewportProps) {
  const problemParts = findings.some((finding) => finding.severity === 'critical')
    ? ['motor', 'arm']
    : [];
  return (
    <div className="viewport">
      <Canvas
        camera={{position: [6, 4, 7], fov: 42}}
        shadows
        onPointerMissed={() => setSelected('')}
      >
        <color attach="background" args={['#171b20']} />
        <ambientLight intensity={1.5} />
        <directionalLight position={[5, 8, 5]} intensity={2} castShadow />
        <Bounds fit clip observe margin={1.4}>
          {fixture.children!.map((part) => (
            <PartMesh
              key={part.id}
              part={part}
              selected={selected === part.id}
              onSelect={setSelected}
              problem={problemParts.includes(part.id)}
            />
          ))}
        </Bounds>
        <Grid
          args={[20, 20]}
          cellColor="#343b43"
          sectionColor="#48515a"
          fadeDistance={16}
        />
        <OrbitControls makeDefault />
      </Canvas>
      <div className="view-cube">TOP<br /><b>FRONT</b></div>
      <div className="view-tools">⌖ Fit &nbsp; ◉ Orbit &nbsp; ⤢ Measure</div>
    </div>
  );
}

type TreeProps = {
  selected: string;
  setSelected: (id: string) => void;
  findings: Finding[];
};

function Tree({selected, setSelected, findings}: TreeProps) {
  return (
    <aside className="left">
      <h3>ARCHIVED PROJECT</h3>
      <div className="tree">
        <b>▾ Assembly</b>
        {fixture.children!.map((part) => (
          <button
            key={part.id}
            className={selected === part.id ? 'active' : ''}
            onClick={() => setSelected(part.id)}
          >
            <span>◇ {part.name}</span>
            <small>{part.badge}</small>
          </button>
        ))}
        <b>▾ Components</b>
        <div className="indent">Disabled — use the Qt review flow</div>
        <b>▾ Tests</b>
        <div className="indent">Disabled until Program 01B</div>
        <b>▾ Historical Findings <em>{findings.length}</em></b>
      </div>
    </aside>
  );
}

type FindingsProps = {
  items: Finding[];
  onFocus: (id: string) => void;
};

function Findings({items, onFocus}: FindingsProps) {
  return (
    <div className="results">
      <div className="results-title">
        <div><small>STORED HISTORICAL RESULTS</small><h2>Read-only findings</h2></div>
        <span>{items.length} results</span>
      </div>
      {items.map((finding) => (
        <article
          key={finding.id}
          className={`finding ${finding.severity}`}
          onClick={() => onFocus('motor')}
        >
          <div className="severity">{finding.severity.replace('_', ' ')}</div>
          <div>
            <h3>{String(finding.data.summary ?? finding.failure_mechanism)}</h3>
            <p>{finding.failure_mechanism}</p>
          </div>
        </article>
      ))}
    </div>
  );
}

export default function App() {
  const [project, setProject] = useState<{id: string; name: string} | null>(null);
  const [assemblyLoaded, setAssemblyLoaded] = useState(false);
  const [selected, setSelected] = useState('');
  const [historicalFindings] = useState<Finding[]>([]);
  const [busy, setBusy] = useState('');
  const [error, setError] = useState('');

  const start = async () => {
    setBusy('Creating archived fixture project…');
    setError('');
    try {
      const created = await api.createProject('Motor-driven arm archived viewer');
      const imported = await api.importFixture(created.id);
      setAssemblyLoaded(Boolean(imported.assembly));
      setProject(created);
    } catch (reason) {
      setError(reason instanceof Error ? reason.message : String(reason));
    } finally {
      setBusy('');
    }
  };

  if (!project) {
    return (
      <main className="welcome archived-welcome">
        <ArchiveBanner />
        <div className="brandmark">P</div>
        <h1>Prometheus</h1>
        <p>Read-only rough-V1 fixture viewer</p>
        <button className="primary hero" onClick={start} disabled={busy !== ''}>
          Load archived fixture
        </button>
        <button className="secondary hero" disabled title="Project-package reopen is not implemented in this archived UI.">
          Open Project
        </button>
        <section>
          <h3>AVAILABLE VIEW</h3>
          <div className="recent">
            <b>Motor-driven arm example</b>
            <span>Geometry only • no execution</span>
          </div>
        </section>
        {busy && <div className="toast">{busy}</div>}
        {error && <div className="toast error-toast">{error}</div>}
      </main>
    );
  }

  const selectedPart = fixture.children!.find((part) => part.id === selected);
  const researchDisabled = disabledActionExplanation('research');
  const runDisabled = disabledActionExplanation('run');
  return (
    <div className="app archived-app">
      <nav>{['File', 'Edit', 'View', 'Project', 'Components', 'Test', 'Results', 'Help'].map((item) => <span key={item}>{item}</span>)}</nav>
      <header className="toolbar">
        <div className="wordmark"><i>P</i><b>Prometheus</b></div>
        <button disabled={assemblyLoaded}>⇧ Import CAD</button>
        <button className="primary" disabled title={researchDisabled}>＋ Add Component</button>
        <button disabled title={researchDisabled}>⌁ Connect</button>
        <button>⌖ Measure</button>
        <button className="primary" disabled title={runDisabled}>▣ Define Test</button>
        <button className="run" disabled title={runDisabled}>▶ Run Checks</button>
      </header>
      <ArchiveBanner />
      <div className="workspace">
        <Tree selected={selected} setSelected={setSelected} findings={historicalFindings} />
        <section className="center">
          <Viewport selected={selected} setSelected={setSelected} findings={historicalFindings} />
          {historicalFindings.length > 0 && (
            <Findings items={historicalFindings} onFocus={setSelected} />
          )}
        </section>
        <aside className="right">
          <div className="tabs"><b>Properties</b><span>Model</span><span>Evidence</span></div>
          {selectedPart ? (
            <>
              <h2>{selectedPart.name}</h2>
              <span className="badge">{selectedPart.badge}</span>
              <dl>
                <dt>Instance</dt><dd>{selectedPart.id}</dd>
                <dt>Selection</dt><dd>1 object</dd>
                <dt>Geometry</dt><dd>Fixture tessellation</dd>
              </dl>
            </>
          ) : (
            <div className="empty">Select a part to inspect the archived geometry.</div>
          )}
          <div className="notice warning archived-limit">
            {researchDisabled}<br /><br />{runDisabled}
          </div>
        </aside>
      </div>
      <footer className="status">
        <span>Selected: {selected || 'None'}</span>
        <span>Units: SI</span>
        <span className="grow">Archived viewer • execution disabled</span>
        <span>0 new findings</span>
      </footer>
    </div>
  );
}
