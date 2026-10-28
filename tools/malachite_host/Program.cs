using System.Collections.Concurrent;
using System.Collections.Immutable;
using System.IO;
using System.Runtime.InteropServices;
using System.Text;
using System.Text.Json;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Interop;
using System.Windows.Media;
using System.Windows.Media.Imaging;
using System.Windows.Threading;
using DatabaseEditor.Modules.V1;
using QuestStudio;

namespace Ataman.MalachiteHost;

// Only the mapper writes MOBs. This host receives Unicode script snapshots,
// and translates the existing module contract into revision-checked RPCs.
static class Program
{
    static readonly ConcurrentDictionary<int, TaskCompletionSource<JsonElement>> replies = new();
    static readonly object outputLock = new();
    static int serial;
    static QuestView? view;
    static Host? host;
    static HwndSource? source;
    static bool verification;
    [DllImport("user32.dll")] static extern bool IsWindow(IntPtr hwnd);
    [DllImport("user32.dll")] static extern IntPtr GetParent(IntPtr hwnd);
    [DllImport("user32.dll")] static extern IntPtr GetWindowDpiAwarenessContext(IntPtr hwnd);
    [DllImport("user32.dll")] static extern IntPtr SetThreadDpiAwarenessContext(IntPtr context);
    [DllImport("user32.dll")] static extern bool SetWindowPos(IntPtr hwnd, IntPtr after, int x, int y, int cx, int cy, uint flags);

    static void Send(object value) { lock (outputLock) { Console.Out.WriteLine(JsonSerializer.Serialize(value)); Console.Out.Flush(); } }
    internal static JsonElement Call(string method, object? data = null)
    {
        int id = Interlocked.Increment(ref serial);
        var promise = new TaskCompletionSource<JsonElement>(TaskCreationOptions.RunContinuationsAsynchronously);
        replies[id] = promise;
        try
        {
            Send(new { call = id, method, data });
            if (!promise.Task.Wait(TimeSpan.FromSeconds(15))) throw new IOException("Маппер не ответил за 15 секунд. Текст сохранён в окне редактора.");
            return promise.Task.GetAwaiter().GetResult();
        }
        finally { replies.TryRemove(id, out _); }
    }

    [STAThread]
    static int Main(string[] args)
    {
        Console.InputEncoding = new UTF8Encoding(false); Console.OutputEncoding = new UTF8Encoding(false);
        verification = args.Contains("--verify");
        try
        {
            var parent = new IntPtr(long.Parse(args[0], System.Globalization.CultureInfo.InvariantCulture));
            if (!IsWindow(parent)) throw new InvalidOperationException("Недействительное окно маппера.");
            // Match the actual Qt parent's DPI mode before creating any WPF HWND.
            SetThreadDpiAwarenessContext(GetWindowDpiAwarenessContext(parent));
            var app = new Application { ShutdownMode = ShutdownMode.OnExplicitShutdown };
            _ = Task.Run(async () =>
            {
                try
                {
                    while (await Console.In.ReadLineAsync() is { } line)
                    {
                        using var json = JsonDocument.Parse(line); var message = json.RootElement.Clone();
                        if (message.TryGetProperty("reply", out var reply))
                        { if (replies.TryGetValue(reply.GetInt32(), out var promise)) promise.TrySetResult(message); }
                        else _ = app.Dispatcher.BeginInvoke(() => Handle(message, parent));
                    }
                }
                catch (Exception ex) { Send(new { fatal = ex.Message }); }
                finally
                {
                    foreach (var p in replies.Values) p.TrySetException(new IOException("Канал маппера закрыт."));
                    _ = app.Dispatcher.BeginInvoke(app.Shutdown);
                }
            });
            app.Run(); view?.Stop(); source?.Dispose(); return 0;
        }
        catch (Exception ex) { Send(new { fatal = ex.ToString() }); return 1; }
    }
    static void Handle(JsonElement m, IntPtr parent)
    {
        int id = m.GetProperty("id").GetInt32();
        try
        {
            string command = m.GetProperty("command").GetString()!;
            switch (command)
            {
                case "open":
                    if (view != null) throw new InvalidOperationException("Документ уже открыт.");
                    host = new Host(); view = new QuestView(host, Path.Combine(Path.GetTempPath(), "Ataman-Malachite-unused-catalog.json"));
                    Encoding.RegisterProvider(CodePagesEncodingProvider.Instance);
                    string path = m.GetProperty("name").GetString()!;
                    var workspace = new Workspace { Name = Path.GetFileName(path), SourcePath = path,
                        Text = m.GetProperty("text").GetString()!, TextEncoding = Encoding.GetEncoding(1251) };
                    view.LoadOpenedFile(new OpenedFile(path + ".malachite", "", ImmutableArray.CreateRange(workspace.SaveProject())));
                    host.Revision = m.GetProperty("revision").GetInt64();
                    host.Set(m.GetProperty("text").GetString()!, host.Revision, m.GetProperty("dirty").GetBoolean(), false);
                    if(view.FindName("EncodingText") is TextBlock encoding)
                        encoding.Text = "MOB · Windows-1251 · Ctrl+S — сохранить MOB";
                    source = new HwndSource(new HwndSourceParameters("Ataman Malachite") {
                        ParentWindow = parent, WindowStyle = 0x40000000 | 0x10000000 | 0x02000000,
                        Width = m.GetProperty("width").GetInt32(), Height = m.GetProperty("height").GetInt32()
                    });
                    source.RootVisual = view;
                    Resize(m); view.UpdateLayout();
                    Send(new { id, ok = true, hwnd = source.Handle.ToInt64().ToString(), embedded = GetParent(source.Handle) == parent, text = view.Workspace.Text }); return;
                case "sync": host!.Set(m.GetProperty("text").GetString()!, m.GetProperty("revision").GetInt64(), m.GetProperty("dirty").GetBoolean()); break;
                case "resize": Resize(m); break;
                case "navigate":
                    Editor.Select(Math.Clamp(m.GetProperty("offset").GetInt32(), 0, Editor.Text.Length), 0); Editor.Focus(); break;
                case "undo": view!.RunCommand(ModuleCommand.Undo); break;
                case "redo": view!.RunCommand(ModuleCommand.Redo); break;
                case "save": view!.RunCommand(ModuleCommand.Save); break;
                case "inspect": break;
                case "crash":
                    if (!verification) throw new InvalidOperationException("Проверочная команда отключена.");
                    Environment.Exit(23); return;
                case "edit":
                    if (!verification) throw new InvalidOperationException("Проверочная команда отключена.");
                    Editor.Select(m.GetProperty("start").GetInt32(), m.GetProperty("length").GetInt32());
                    Editor.SelectedText = m.GetProperty("text").GetString()!; break;
                case "render":
                    if (!verification) throw new InvalidOperationException("Проверочная команда отключена.");
                    view!.UpdateLayout();
                    var dpi = source!.CompositionTarget.TransformToDevice;
                    var bitmap = new RenderTargetBitmap(Math.Max(1,(int)Math.Round(view.ActualWidth*dpi.M11)), Math.Max(1,(int)Math.Round(view.ActualHeight*dpi.M22)), 96*dpi.M11, 96*dpi.M22, PixelFormats.Pbgra32);
                    bitmap.Render(view); var encoder = new PngBitmapEncoder(); encoder.Frames.Add(BitmapFrame.Create(bitmap));
                    using (var file = File.Create(m.GetProperty("path").GetString()!)) encoder.Save(file); break;
                case "close": view?.Stop(); Send(new { id, ok = true }); Application.Current.Shutdown(); return;
                default: throw new InvalidOperationException("Неизвестная команда.");
            }
            Send(new { id, ok = true, text = Editor.Text, revision = host!.Revision, dirty = host.Current?.IsDirty, error = host.LastError });
        }
        catch (Exception ex) { Send(new { id, ok = false, error = ex.ToString() }); }
    }
    static TextBox Editor => (TextBox)view!.FindName("CodeEditor");
    static void Resize(JsonElement m)
    {
        if (source == null || view == null) return;
        int w = Math.Max(1, m.GetProperty("width").GetInt32()), h = Math.Max(1, m.GetProperty("height").GetInt32());
        SetWindowPos(source.Handle, IntPtr.Zero, 0, 0, w, h, 0x0014);
        var scale = source.CompositionTarget.TransformFromDevice;
        var size = scale.Transform(new Point(w,h)); view.Width = size.X; view.Height = size.Y;
    }
}

sealed class Host : IModuleDocumentHost, IWorkspaceReader, IModuleFiles, IModuleDocuments, IModuleTransactions, IModuleSave
{
    readonly Guid session = Guid.NewGuid();
    public long Revision;
    public DocumentSnapshot? Current;
    public string LastError = "";
    public IWorkspaceReader Workspace => this;
    public IModuleFiles Files => this;
    public IModuleDocuments Documents => this;
    public IModuleTransactions Transactions => this;
    public IModuleSave Save => this;
    public ImmutableArray<string> Capabilities => ["workspace.read.v1", "documents.session.v1", "transactions.v1", "saves.artifact-kind.v1"];
    event EventHandler<WorkspaceEvent>? IWorkspaceReader.Changed { add { workspaceChanged += value; } remove { workspaceChanged -= value; } }
    event EventHandler<WorkspaceEvent>? workspaceChanged;
    public event EventHandler<DocumentEvent>? Changed;
    public WorkspaceSnapshot Snapshot() => new(new(session, 1, Revision), "MOB маппера", Current?.IsDirty ?? false, []);
    public Result Navigate(RecordRef reference) => new(ResultStatus.Unsupported, "Переходы к БД не входят в прототип.");
    public void Report(string message) => LastError = message;
    public OpenedFile? Open(string filter) { Report("Откройте MOB в маппере."); return null; }
    public Result Export(string name, string filter, ReadOnlyMemory<byte> bytes) => new(ResultStatus.Unsupported, "Сохраните MOB через маппер.");
    public Result RegisterCodec(string owner, string format, int version, IDocumentCodec codec) => Result.Ok;
    public DocumentSnapshot Open(DocumentRegistration r)
    {
        if (Current != null) throw new InvalidOperationException("Прототип редактирует одну MOB в каждом окне.");
        return Current = new(Guid.NewGuid(), r.OwnerId, r.FormatId, r.StateVersion, r.DisplayName, Revision, r.Text, r.State, r.InitiallyDirty, "");
    }
    public DocumentSnapshot? Get(Guid id) => Current?.DocumentId == id ? Current : null;
    public ImmutableArray<DocumentSnapshot> List(string owner) => Current?.OwnerId == owner ? [Current] : [];
    public Result Close(Guid id, long revision, bool discard)
    {
        if (Current?.DocumentId != id || Current.Revision != revision) return new(ResultStatus.Conflict, "Документ изменился.");
        Current = null; return Result.Ok;
    }
    public void Set(string text, long revision, bool dirty, bool notify = true)
    {
        Revision = revision;
        if (Current == null) return;
        Current = Current with { Text = text, Revision = revision, IsDirty = dirty };
        workspaceChanged?.Invoke(this, new(new(session,1,Revision), "script"));
        if (notify) Changed?.Invoke(this, new(Current.DocumentId,Revision,"script"));
    }
    Result Remote(string method, object? data = null)
    {
        try
        {
            var result = Program.Call(method, data);
            if (!result.GetProperty("ok").GetBoolean()) { LastError = result.GetProperty("error").GetString()!; return new(ResultStatus.Conflict, LastError); }
            LastError = "";
            Set(result.GetProperty("text").GetString()!, result.GetProperty("revision").GetInt64(), result.GetProperty("dirty").GetBoolean());
            return Result.Ok;
        }
        catch (Exception ex) { LastError = ex.Message; return new(ResultStatus.Failed, LastError); }
    }
    public Result Apply(ChangeSet change)
    {
        if (change.Operations.Length != 1 || change.Operations[0] is not UpdateDocument update ||
            Current?.DocumentId != update.DocumentId || Current.Revision != update.ExpectedRevision)
            return new(ResultStatus.Conflict, "Устаревшая редакция сценария.");
        // State is retained locally; the mapper receives only the script delta.
        var previous = Current;
        Current = Current with { State = update.State };
        var result = Remote("apply", new { text = update.Text, revision = update.ExpectedRevision });
        if (!result.Succeeded) Current = previous;
        return result;
    }
    public Result Undo() => Remote("undo");
    public Result Redo() => Remote("redo");
    public Result Export(Guid id, string kind, string name, string filter) => new(ResultStatus.Unsupported, "Сохраните MOB через маппер.");
    public SaveReport SaveAll()
    {
        var result = Remote("save");
        return new([new(Current?.DocumentId ?? Guid.Empty, Current?.DisplayName ?? "", result.Succeeded ? "saved" : "failed", result.Message)], result.Succeeded);
    }
}
