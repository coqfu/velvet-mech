import { BootIntro } from "./components/BootIntro";
import { Nav } from "./components/Nav";
import { StatusBar } from "./components/StatusBar";
import { Toasts } from "./components/Toasts";
import { TopBar } from "./components/TopBar";
import { AdvancedPage } from "./pages/Advanced";
import { EffectsPage } from "./pages/Effects";
import { KeymapPage } from "./pages/Keymap";
import { LightingPage } from "./pages/Lighting";
import { MacrosPage } from "./pages/Macros";
import { ProfilesPage } from "./pages/Profiles";
import { SoundPage } from "./pages/Sound";
import { useStore } from "./state/store";

export default function App() {
  const { page } = useStore();

  return (
    <div className="shell">
      <BootIntro />
      <TopBar />
      <Nav />
      {page === "lighting" && <LightingPage />}
      {page === "effects" && <EffectsPage />}
      {page === "sound" && <SoundPage />}
      {page === "keymap" && <KeymapPage />}
      {page === "macros" && <MacrosPage />}
      {page === "profiles" && <ProfilesPage />}
      {page === "advanced" && <AdvancedPage />}
      <StatusBar />
      <Toasts />
    </div>
  );
}
