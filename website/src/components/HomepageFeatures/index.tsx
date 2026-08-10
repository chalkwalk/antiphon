import type { ReactNode } from 'react';
import clsx from 'clsx';
import Heading from '@theme/Heading';
import styles from './styles.module.css';

type FeatureItem = {
  title: string;
  description: ReactNode;
};

const FeatureList: FeatureItem[] = [
  {
    title: 'It lives in your session',
    description: (
      <>
        Every other NINJAM client is a standalone application that wants your
        sound card. Antiphon is a plugin, so the jam happens in the project you
        were already working in, with your own effects, monitoring and recorder.
      </>
    ),
  },
  {
    title: 'Everyone arrives as a stem',
    description: (
      <>
        Each remote player&rsquo;s channel can be routed to its own output bus.
        Your DAW records the jam as separate tracks, ready to mix, rather than as
        one flattened mixdown you cannot take apart afterwards.
      </>
    ),
  },
  {
    title: 'The delay is the instrument',
    description: (
      <>
        You hear everyone one interval late, locked to the beat. That is not
        latency to be fought -- it is what makes playing with someone on another
        continent musical instead of merely possible.
      </>
    ),
  },
  {
    title: 'Built to be heard, not just seen',
    description: (
      <>
        Every control carries a name a screen reader can announce, checked by a
        test that fails the build if one does not. What is proven and what is not
        is written down honestly.
      </>
    ),
  },
];

function Feature({ title, description }: FeatureItem) {
  return (
    <div className={clsx('col col--6')}>
      <div className={styles.feature}>
        <Heading as="h3">{title}</Heading>
        <p>{description}</p>
      </div>
    </div>
  );
}

export default function HomepageFeatures(): ReactNode {
  return (
    <section className={styles.features}>
      <div className="container">
        <div className="row">
          {FeatureList.map((props, idx) => (
            <Feature key={idx} {...props} />
          ))}
        </div>
      </div>
    </section>
  );
}
