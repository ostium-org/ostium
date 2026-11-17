// i18n — provider + hook.
//
// `I18nProvider` owns the active locale, persists it to device preferences, and exposes a `t`
// function that screens use to look up strings. `t` supports simple `{placeholder}` substitution
// (e.g. `t('settings.account.signedInAs', { email: user.email })`).
//
// The provider is synchronous after mount: it reads the saved locale on first render (falling back
// to the device locale, then English) and hydrates from storage in the background. This keeps the
// first paint fast and avoids a flash of the wrong language.
import {
  createContext,
  useCallback,
  useContext,
  useEffect,
  useMemo,
  useState,
  type ReactNode,
} from "react";

import { prefsStorage } from "@/services/prefs-storage";
import {
  SUPPORTED_LOCALES,
  translations,
  type Locale,
  type TranslationKey,
} from "@/i18n/translations";

const LOCALE_STORAGE_KEY = "ostium.prefs.locale";
const MAX_RECENT = 8;

export type RecentAction = {
  id: string;
  // A translation key (e.g. 'action.signIn') so the label re-renders when the locale changes.
  labelKey: TranslationKey;
  // Optional detail shown after the label (e.g. a house name). Already a display string.
  detail?: string;
  at: number; // epoch ms
};

type I18nContextValue = {
  locale: Locale;
  setLocale: (locale: Locale) => void;
  t: (key: TranslationKey, vars?: Record<string, string | number>) => string;
  recentActions: RecentAction[];
  recordAction: (labelKey: TranslationKey, detail?: string) => void;
  clearActions: () => void;
};

const I18nContext = createContext<I18nContextValue | null>(null);

function isSupportedLocale(value: unknown): value is Locale {
  return (
    typeof value === "string" && SUPPORTED_LOCALES.some((l) => l.code === value)
  );
}

// Best-effort guess of the device language from the browser locale (web) — native has no cheap
// synchronous locale API without a dependency, so we fall back to English there.
function detectDeviceLocale(): Locale {
  if (
    typeof navigator !== "undefined" &&
    typeof navigator.language === "string"
  ) {
    const code = navigator.language.slice(0, 2).toLowerCase();
    if (isSupportedLocale(code)) return code;
  }
  return "en";
}

export function I18nProvider({ children }: { children: ReactNode }) {
  const [locale, setLocaleState] = useState<Locale>(() => detectDeviceLocale());
  const [recentActions, setRecentActions] = useState<RecentAction[]>([]);

  // Hydrate the saved locale + recent actions once on mount.
  useEffect(() => {
    let stopped = false;
    void (async () => {
      try {
        const savedLocale = await prefsStorage.getItem(LOCALE_STORAGE_KEY);
        if (!stopped && isSupportedLocale(savedLocale))
          setLocaleState(savedLocale);

        const savedActions = await prefsStorage.getItem("ostium.prefs.recent");
        if (!stopped && savedActions) {
          const parsed: unknown = JSON.parse(savedActions);
          if (Array.isArray(parsed)) {
            const valid = parsed.filter(
              (a): a is RecentAction =>
                !!a &&
                typeof a === "object" &&
                typeof (a as RecentAction).id === "string" &&
                typeof (a as RecentAction).labelKey === "string" &&
                typeof (a as RecentAction).at === "number",
            );
            setRecentActions(valid.slice(0, MAX_RECENT));
          }
        }
      } catch {
        // Corrupt or unavailable storage: keep the detected defaults.
      }
    })();
    return () => {
      stopped = true;
    };
  }, []);

  const setLocale = useCallback((next: Locale) => {
    setLocaleState(next);
    void prefsStorage.setItem(LOCALE_STORAGE_KEY, next).catch(() => {});
  }, []);

  const persistActions = useCallback((actions: RecentAction[]) => {
    void prefsStorage
      .setItem("ostium.prefs.recent", JSON.stringify(actions))
      .catch(() => {});
  }, []);

  const recordAction = useCallback(
    (labelKey: TranslationKey, detail?: string) => {
      setRecentActions((current) => {
        const next: RecentAction[] = [
          {
            id: `${Date.now()}-${Math.random().toString(36).slice(2)}`,
            labelKey,
            detail,
            at: Date.now(),
          },
          ...current,
        ].slice(0, MAX_RECENT);
        persistActions(next);
        return next;
      });
    },
    [persistActions],
  );

  const clearActions = useCallback(() => {
    setRecentActions([]);
    void prefsStorage.removeItem("ostium.prefs.recent").catch(() => {});
  }, []);

  const t = useCallback(
    (key: TranslationKey, vars?: Record<string, string | number>): string => {
      const template = translations[locale][key] ?? translations.en[key] ?? key;
      if (!vars) return template;
      return template.replace(/\{(\w+)\}/g, (match, name: string) =>
        Object.prototype.hasOwnProperty.call(vars, name)
          ? String(vars[name])
          : match,
      );
    },
    [locale],
  );

  const value = useMemo<I18nContextValue>(
    () => ({ locale, setLocale, t, recentActions, recordAction, clearActions }),
    [locale, setLocale, t, recentActions, recordAction, clearActions],
  );

  return <I18nContext.Provider value={value}>{children}</I18nContext.Provider>;
}

export function useI18n(): I18nContextValue {
  const context = useContext(I18nContext);
  if (!context) throw new Error("useI18n must be used inside I18nProvider");
  return context;
}
