import type { SidebarsConfig } from '@docusaurus/plugin-content-docs';

const sidebars: SidebarsConfig = {
  docsSidebar: [
    'intro',
    {
      type: 'category',
      label: 'Getting started',
      collapsed: false,
      items: ['getting-started', 'your-first-jam'],
    },
    {
      type: 'category',
      label: 'Using Antiphon',
      collapsed: false,
      items: ['routing-stems', 'daw-sync', 'chat-and-voting'],
    },
    'accessibility',
    'troubleshooting',
    'developer-guide',
  ],
};

export default sidebars;
