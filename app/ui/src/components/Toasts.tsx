import { useStore } from "../state/store";

export function Toasts() {
  const { toasts } = useStore();
  return (
    <div className="toasts">
      {toasts.map((t) => (
        <div key={t.id} className="toast" data-kind={t.kind}>
          {t.msg}
        </div>
      ))}
    </div>
  );
}
