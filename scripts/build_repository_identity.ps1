function Get-EnduranceGitRepositoryIdentity {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Root
    )

    try {
        $dotGit = Join-Path $Root '.git'
        if (Test-Path -LiteralPath $dotGit -PathType Container -ErrorAction Stop) {
            $headPath = Join-Path $dotGit 'HEAD'
            if (-not (Test-Path -LiteralPath $headPath -PathType Leaf -ErrorAction Stop)) {
                return $null
            }
            $head = (Get-Content -LiteralPath $headPath -Raw -ErrorAction Stop).Trim()
            return "directory|$head"
        }

        if (-not (Test-Path -LiteralPath $dotGit -PathType Leaf -ErrorAction Stop)) {
            return $null
        }
        # Git writes linked-worktree pointer files as UTF-8. Windows
        # PowerShell 5.1 otherwise decodes an unmarked file through the active
        # ANSI code page and corrupts Unicode repository paths.
        $pointer = (Get-Content -LiteralPath $dotGit -Raw -Encoding UTF8 -ErrorAction Stop).Trim()
        if ($pointer -notmatch '^gitdir:\s*(.+)$') {
            return $null
        }
        $gitDirectory = $Matches[1].Trim()
        if (-not [IO.Path]::IsPathRooted($gitDirectory)) {
            $gitDirectory = Join-Path $Root $gitDirectory
        }
        $gitDirectory = (Resolve-Path -LiteralPath $gitDirectory -ErrorAction Stop).Path
        $headPath = Join-Path $gitDirectory 'HEAD'
        if (-not (Test-Path -LiteralPath $headPath -PathType Leaf -ErrorAction Stop)) {
            return $null
        }
        $head = (Get-Content -LiteralPath $headPath -Raw -ErrorAction Stop).Trim()
        return "worktree|$($gitDirectory.TrimEnd('\'))|$head"
    }
    catch {
        # Repository-drive discovery is a best-effort scan. Inaccessible or
        # malformed candidates are not identities and must not abort a build.
        return $null
    }
}
