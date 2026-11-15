// Cover effects ported from MyGo Music (https://github.com/egoist/mygo-music, AGPL-3.0, © egoist):
// - FeatureStack: the home feature card's stack (internal/app/ui_daily.go, featureCard / dailyStack /
//   drawRecord): the first cover as a sleeve with its record peeking out, the next two behind it. Under
//   the pointer the covers fan and the record slides out, turning; while its songs play the record is out
//   and turns on, and grooves ripple out from it across the card.
// - TiltCover: the Now Playing cover (internal/app/ui_cover.go, npCover): it tilts toward the pointer in
//   depth with a glare following the pointer, a little nearer under it and smaller while paused.
// MyGo draws with its own Go UI toolkit; these are React / CSS reimplementations of the same measures.
import { useEffect, useRef, useState } from 'react';
import { motion, useMotionTemplate, useMotionValue, useSpring, type MotionValue } from 'motion/react';

// The stack's measures (ui_daily.go): laid out by hand inside a 300 × 156 box.
const STACK_WIDTH = 300, SLEEVE_LEFT = 64, SLEEVE = 156, RECORD = 146;
const RIPPLE_GAP = 18, RIPPLE_PERIOD = 6; // a groove every 6 s at a turntable's speed
const recordOut = (out: number) => 30 + 46 * out;
const stackSpring = { type: 'spring', duration: .6, bounce: .3 } as const;
const recordSpring = { type: 'spring', duration: .7, bounce: .25 } as const;

export interface StackCover { id: string; src?: string; label: string }

/** Turns the record: its speed eases toward `target` (1 at 33⅓ rpm) and glides to a stop; frames run only while it turns. */
function useSpin(target: number) {
  const angle = useMotionValue(0), ripple = useMotionValue(0);
  const state = useRef({ speed: 0, last: 0, frame: 0, target });
  state.current.target = target;
  useEffect(() => {
    const s = state.current;
    if (s.frame || (target === 0 && s.speed === 0)) return;
    const step = (now: number) => {
      const dt = !s.last || now - s.last > 200 ? 1 / 60 : (now - s.last) / 1000;
      s.last = now;
      s.speed += (s.target - s.speed) * Math.min(1, 2.2 * dt);
      if (Math.abs(s.target - s.speed) < .002) s.speed = s.target;
      angle.set((angle.get() + s.speed * dt * 72) % 360); // 12 rpm: MyGo's 33⅓ looked hurried at this size
      ripple.set((ripple.get() + s.speed * dt * RIPPLE_GAP / RIPPLE_PERIOD) % RIPPLE_GAP);
      if (s.speed > 0) s.frame = requestAnimationFrame(step);
      else { s.frame = 0; s.last = 0; }
    };
    s.frame = requestAnimationFrame(step);
  }, [target, angle, ripple]);
  useEffect(() => () => cancelAnimationFrame(state.current.frame), []);
  return { angle, ripple };
}

/** The record: grooves catching a light that stays put, round a label (the cover) that turns. */
function Record({ src, angle }: { src?: string; angle: MotionValue<number> }) {
  return <div className="feature-record">
    <motion.span className="feature-record-label" style={{ rotate: angle }}>{src && <img src={src} alt="" draggable={false}/>}</motion.span>
    <i className="feature-record-hole"/>
  </div>;
}

/**
 * The covers of the hero card. `hovered`: the pointer is on the card (covers fan, record out a little,
 * turning slowly); `ours`: the card's songs are what plays (record out); `playing`: and not paused (it turns).
 * `record`: the song in the player bar, whose cover is the record's label (else the first cover's).
 */
export function FeatureStack({ covers, record, hovered, ours, playing }: { covers: StackCover[]; record?: { src?: string }; hovered: boolean; ours: boolean; playing: boolean }) {
  const still = useReducedMotion();
  const fanned = hovered && !still;
  const out = ours ? 1 : hovered ? .6 : 0;
  const { angle, ripple } = useSpin(still ? 0 : playing ? 1 : hovered ? .12 : 0);
  const rippleTemplate = useMotionTemplate`${ripple}px`;
  const [first, ...rest] = covers;
  const recordX = SLEEVE_LEFT + SLEEVE / 2 + recordOut(out) - RECORD / 2;
  return <div className="feature-stack" style={{ width: STACK_WIDTH, height: SLEEVE }} aria-hidden="true">
    {/* Grooves rippling out from the record across the card, fainter as they go. */}
    <motion.div className="feature-ripples" initial={false} animate={{ x: SLEEVE_LEFT + SLEEVE / 2 + recordOut(out) }} transition={recordSpring} style={{ '--ripple': rippleTemplate } as React.CSSProperties}>
      {Array.from({ length: 14 }, (_, k) => <i key={k} style={{ '--k': k } as React.CSSProperties}/>)}
    </motion.div>
    {rest.slice(0, 2).map((cover, index) => ({ cover, i: index + 1 })).reverse().map(({ cover, i }) => {
      const size = SLEEVE - 18 * i;
      return <motion.div key={cover.id} className="feature-behind" style={{ left: SLEEVE_LEFT - 30 * i, top: (SLEEVE - size) / 2, width: size, height: size, opacity: 1 - .12 * i }}
        initial={false} animate={fanned ? { rotate: -9 * i, x: -10 * i, y: -2 } : { rotate: -5 * i, x: 0, y: 2 }} transition={stackSpring}>
        {cover.src && <img src={cover.src} alt="" draggable={false}/>}
      </motion.div>;
    })}
    <motion.div className="feature-record-slot" style={{ top: (SLEEVE - RECORD) / 2, width: RECORD, height: RECORD }} initial={false} animate={{ x: recordX }} transition={recordSpring}>
      <Record src={record ? record.src : first?.src} angle={angle}/>
    </motion.div>
    <motion.div className="feature-sleeve" style={{ left: SLEEVE_LEFT, width: SLEEVE, height: SLEEVE }} initial={false} animate={{ rotate: fanned ? -2 : 0, y: fanned ? -3 : 0 }} transition={stackSpring}>
      {first?.src && <img src={first.src} alt={first.label} draggable={false}/>}
      <i className="feature-sleeve-gloss"/>
    </motion.div>
  </div>;
}

function useReducedMotion() {
  const [reduce, setReduce] = useState(() => matchMedia('(prefers-reduced-motion: reduce)').matches);
  useEffect(() => {
    const query = matchMedia('(prefers-reduced-motion: reduce)');
    const change = () => setReduce(query.matches);
    query.addEventListener('change', change);
    return () => query.removeEventListener('change', change);
  }, []);
  return reduce;
}

const tilt = { stiffness: 170, damping: 19, mass: 1 }; // ≈ MyGo's 500 ms spring, bounce .2
const clamp1 = (v: number) => Math.max(-1, Math.min(1, v));

/** Now Playing's cover: tilts toward the pointer (±7°) with a glare following it; smaller while paused. */
export function TiltCover({ playing, className = '', children }: { playing: boolean; className?: string; children: React.ReactNode }) {
  const still = useReducedMotion();
  const [over, setOver] = useState(false);
  const rx = useSpring(0, tilt), ry = useSpring(0, tilt), gx = useSpring(0, tilt), gy = useSpring(0, tilt);
  const shine = useSpring(0, { stiffness: 120, damping: 24 });
  const glare = useMotionTemplate`radial-gradient(circle at ${gx}px ${gy}px, rgb(255 255 255 / .1), rgb(255 255 255 / .06) 18%, transparent 46%)`;
  const move = (event: React.PointerEvent<HTMLDivElement>) => {
    const box = event.currentTarget.getBoundingClientRect();
    const px = event.clientX - box.left, py = event.clientY - box.top;
    if (!over) { gx.jump(px); gy.jump(py); setOver(true); shine.set(1); }
    gx.set(px); gy.set(py);
    if (still) return;
    ry.set(clamp1(px / box.width * 2 - 1) * 7);
    rx.set(-clamp1(py / box.height * 2 - 1) * 7);
  };
  const leave = () => { setOver(false); rx.set(0); ry.set(0); shine.set(0); };
  const scale = (playing ? 1 : .88) * (over ? 1.02 : 1);
  return <motion.div className={`tilt-cover ${className}`} onPointerMove={move} onPointerLeave={leave}
    style={{ rotateX: rx, rotateY: ry, transformPerspective: 900 }} initial={false} animate={{ scale }} transition={{ type: 'spring', duration: .6, bounce: .2 }}>
    {children}
    <motion.span className="tilt-cover-glare" style={{ background: glare, opacity: shine }}/>
  </motion.div>;
}
