// MAIN APP LAYOUT — the signed-in area
// "(app)" is a route group: the parentheses mean it doesn't appear in URLs, so these screens still
// live at "/", "/qr-code" and "/settings". Grouping them lets the root layout guard them all at once
// (only reachable when signed in) and keeps the tab bar off the login screen.
// Later this group can be split by role, e.g. "(tenant)" and "(manager)", as application/README.md plans.

import AppTabs from '@/components/app-tabs';

export default function AppLayout() {
  return <AppTabs />;
}