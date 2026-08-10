import { themes as prismThemes } from 'prism-react-renderer';
import type { Config } from '@docusaurus/types';
import type * as Preset from '@docusaurus/preset-classic';

const config: Config = {
  title: 'Antiphon',
  tagline: 'Jam with strangers, from inside your DAW',
  favicon: 'img/favicon.ico',

  future: {
    v4: true,
  },

  url: 'https://antiphon.chalkwalkmusic.com',
  baseUrl: '/',

  organizationName: 'chalkwalk',
  projectName: 'antiphon',

  onBrokenLinks: 'throw',

  i18n: {
    defaultLocale: 'en',
    locales: ['en'],
  },

  presets: [
    [
      'classic',
      {
        docs: {
          sidebarPath: './sidebars.ts',
          routeBasePath: 'docs',
          editUrl: 'https://github.com/chalkwalk/antiphon/tree/main/website/',
        },
        blog: false,
        theme: {
          customCss: './src/css/custom.css',
        },
      } satisfies Preset.Options,
    ],
  ],

  themeConfig: {
    image: 'img/social-card.png',
    colorMode: {
      respectPrefersColorScheme: true,
    },
    navbar: {
      title: 'Antiphon',
      // Decorative: the title beside it already carries the name, so an alt
      // text here would only make a screen reader say "Antiphon" twice.
      logo: {
        alt: '',
        src: 'img/logo.png',
      },
      items: [
        {
          type: 'docSidebar',
          sidebarId: 'docsSidebar',
          position: 'left',
          label: 'Guide',
        },
        {
          href: 'https://github.com/chalkwalk/antiphon',
          label: 'GitHub',
          position: 'right',
        },
      ],
    },
    footer: {
      style: 'dark',
      links: [
        {
          title: 'Guide',
          items: [
            { label: 'Getting started', to: '/docs/getting-started' },
            { label: 'Your first jam', to: '/docs/your-first-jam' },
            { label: 'Recording stems', to: '/docs/routing-stems' },
            { label: 'Troubleshooting', to: '/docs/troubleshooting' },
          ],
        },
        {
          title: 'Project',
          items: [
            { label: 'GitHub', href: 'https://github.com/chalkwalk/antiphon' },
            {
              label: 'Contributing',
              href: 'https://github.com/chalkwalk/antiphon/blob/main/CONTRIBUTING.md',
            },
            {
              label: 'Accessibility',
              to: '/docs/accessibility',
            },
          ],
        },
        {
          title: 'NINJAM',
          items: [
            { label: 'What NINJAM is', href: 'https://www.cockos.com/ninjam/' },
            { label: 'Server list (ninbot)', href: 'https://ninbot.com/' },
          ],
        },
      ],
      copyright: `Antiphon is free software under the GPLv3. Copyright &copy; ${new Date().getFullYear()} ChalkWalk.`,
    },
    prism: {
      theme: prismThemes.github,
      darkTheme: prismThemes.dracula,
      additionalLanguages: ['bash', 'cmake'],
    },
  } satisfies Preset.ThemeConfig,
};

export default config;
