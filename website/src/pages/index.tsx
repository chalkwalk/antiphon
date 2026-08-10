import type { ReactNode } from 'react';
import clsx from 'clsx';
import Link from '@docusaurus/Link';
import useDocusaurusContext from '@docusaurus/useDocusaurusContext';
import Layout from '@theme/Layout';
import HomepageFeatures from '@site/src/components/HomepageFeatures';
import Heading from '@theme/Heading';

import styles from './index.module.css';

function HomepageHeader() {
  const { siteConfig } = useDocusaurusContext();
  return (
    <header className={clsx('hero', styles.heroBanner)}>
      <div className="container">
        <Heading as="h1" className="hero__title">
          {siteConfig.title}
        </Heading>
        <p className="hero__subtitle">{siteConfig.tagline}</p>
        <div className={styles.buttons}>
          <Link
            className="button button--primary button--lg"
            to="/docs/getting-started">
            Get started
          </Link>
          <Link
            className="button button--secondary button--lg"
            to="/docs/your-first-jam">
            How a jam works
          </Link>
        </div>
        <div className={clsx('margin-top--lg', styles.heroScreenshot)}>
          <img
            src="img/antiphon.png"
            alt="The Antiphon window: local channel strips on the left, remote players in the centre, chat on the right"
            className={styles.screenshotImage}
          />
        </div>
      </div>
    </header>
  );
}

export default function Home(): ReactNode {
  return (
    <Layout
      title="Jam with strangers, from inside your DAW"
      description="Antiphon is a NINJAM client shaped as a VST3, CLAP and standalone audio plugin. Play with people over the internet without leaving your session, and record every player as a separate stem.">
      <HomepageHeader />
      <main>
        <HomepageFeatures />
      </main>
    </Layout>
  );
}
