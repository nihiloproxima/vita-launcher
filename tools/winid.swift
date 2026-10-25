// Affiche l'identifiant de la fenêtre du processus donné (pour screencapture -l). usage : swift winid.swift <pid>
import CoreGraphics
let pid = Int(CommandLine.arguments[1])!
let list = CGWindowListCopyWindowInfo([.optionOnScreenOnly], kCGNullWindowID) as! [[String: Any]]
if let w = list.first(where: { ($0[kCGWindowOwnerPID as String] as? Int) == pid }) {
    print(w[kCGWindowNumber as String]!)
}
