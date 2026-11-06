// Copyright 2025 OfficeCLI (officecli.ai)
// SPDX-License-Identifier: Apache-2.0

namespace OfficeCli.Core;

internal static class NetworkPolicy
{
    public const string AllowNetworkEnv = "OFFICECLI_ALLOW_NETWORK";
    public const string SkipUpdateEnv = "OFFICECLI_SKIP_UPDATE";
    public const string NoNetworkEnv = "OFFICECLI_NO_NETWORK";

    /// <summary>
    /// Polymech default: OfficeCLI is offline unless explicitly opted in.
    /// Set OFFICECLI_ALLOW_NETWORK=1 to permit update checks and external preview assets.
    /// OFFICECLI_NO_NETWORK=1 and OFFICECLI_SKIP_UPDATE=1 are kept as explicit hard disables.
    /// </summary>
    public static bool AllowNetwork =>
        Environment.GetEnvironmentVariable(NoNetworkEnv) != "1" &&
        Environment.GetEnvironmentVariable(AllowNetworkEnv) == "1";

    public static bool AllowUpdates =>
        AllowNetwork && Environment.GetEnvironmentVariable(SkipUpdateEnv) != "1";

    public static bool AllowPreviewExternalAssets => AllowNetwork;
}
