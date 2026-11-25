# Ostium user guide

Ostium lets you use a QR code to access your house and view the properties linked to your account. This guide explains how to get started, use your code, and sort out common problems. The steps apply to the mobile app and the browser version.

The app is still a prototype. QR access needs a configured door reader, and the current hardware uses an LED to show an unlock. A real lock relay is not connected yet.

## Getting started

Open the app or the browser link provided by the person running your Ostium project. You'll need an internet connection to sign in, load your houses, and get a fresh QR code.

If you're setting up the application yourself, follow the [client setup guide](../application/README.md). Your property manager or project administrator handles account access and door setup.

### Create an account

1. On the sign-in screen, choose **CREATE ACCOUNT**.
2. Choose **Tenant** or **Landlord**.
3. Add your full name if you want to. This field is optional.
4. Enter your email address and a password with at least 8 characters. The service may ask for a stronger password.
5. Enter the same password in **Confirm password**, then choose **CREATE ACCOUNT**.
6. If the app asks you to confirm your email, open the confirmation link in your inbox. Come back to the app and sign in.

Choosing **Landlord** records your account choice, but you still start with tenant permissions. An administrator needs to give you manager access before you can add houses.

Creating an account doesn't automatically give you access to a house. Ask your property manager to arrange access if your list is empty.

### Sign in

Enter your email address and password, then choose **SIGN IN**. You can use **SHOW** to check your password and **HIDE** to cover it again.

If the app says your session has expired, sign in again. If it says an active profile is required, contact your project administrator.

## Finding your way around

There are three main tabs:

| Tab | What you can do |
| --- | --- |
| **Home** | See your houses and open their details. The page heading is **HOUSES**. |
| **QR Code** | Select a house and show its current access code. |
| **Settings** | Choose a light or dark theme, check your signed-in email, and sign out. |

Tenants see active houses where their access has started and hasn't expired. Managers and admins can see the active properties available to their account and use **Add house**.

## Viewing a house

1. Open **Home**.
2. Tap or click a house card to see its name, address, and property status.
3. Choose **Unlock [house name]** to open that house's QR code.
4. Use **Back to houses** to return to the list.

The **Unlock** button opens the QR screen. You'll still need to show the code to the door reader.

Pull down on the Home list to refresh it on mobile. The list also refreshes when you return to the screen. If loading fails, choose **Retry houses**.

The property status section isn't connected to live door or temperature readings yet. **Status unavailable** doesn't tell you whether the door is locked, and **21.5 C** is a sample reading.

## Using your QR code

1. Open **QR Code**, or choose **Unlock [house name]** from a house's details.
2. Check the house name and address. To switch houses, choose **Change**, then select the house you need.
3. Wait for the code to appear, then hold it in front of the door camera with the whole code visible.
4. Keep the screen open while the reader scans it.
5. When the app receives a successful scan report from the door, it shows **Access granted**, the house, and the scan time.
6. Choose **Back to QR code** if you need a new code.

The **Refreshes in** timer shows how long the current code has left. Codes normally last up to 60 seconds and refresh automatically. You can also choose **Refresh QR code** yourself. Use the code on screen rather than saving a screenshot, since an old code can expire.

If a code can't load, choose **Retry QR code**. If the app can't check the scan result, choose **Retry confirmation**. A code appearing on screen only means it was created; **Access granted** appears after the door reports a successful scan. That report doesn't confirm physical lock movement.

Door scanning depends on the reader's setup and camera integration. If it doesn't respond, ask the person running the project to check the door unit. After a door restart, refresh your code before scanning again.

## Adding a house

Managers and admins can add a house using a live account:

1. Open **Home**, then choose **Add house**.
2. Enter a **House name** and **Address**. Both are required.
3. Fill in **Building**, **Floor**, and **Notes** if needed.
4. Choose **Save house** and wait for the app to return to the house list.

If saving fails, check the message on screen. A permissions error needs your project administrator's help.

Adding a house doesn't assign tenants or configure a door reader. Live tenant access changes aren't available in the app yet, so ask your project administrator to arrange them separately.

## Changing the theme and signing out

Open **Settings** and choose **LIGHT** or **DARK** under **Theme**.

The **Account** section shows the email you're signed in with. Choose **SIGN OUT** when you're finished, especially on a shared device.

## Trying the demo

If the project has been started in demo mode, you can use these example accounts:

| Account | Email | Password |
| --- | --- | --- |
| Tenant | `tenant@example.com` | `tenant-pass` |
| Manager | `manager@example.com` | `manager-pass` |

Demo mode has example houses and local tenant memberships. The demo manager can open a house, enter an email under **Grant house access**, choose **Add user**, or use **Remove** beside an existing member. These changes only affect the local demo; they don't update real accounts or door access.

Demo QR codes are labelled **Demo code — door unlocking is not connected.** They don't unlock a door or show a confirmed access result. Saving a new house requires a live account.

## If something goes wrong

| What you see | What to do |
| --- | --- |
| **Incorrect email or password.** | Check your email and password, then try again. If you just signed up, check whether you need to confirm your email first. |
| **Too many attempts.** | Wait a moment before trying again. |
| **The app isn't connected to a server yet.** | Ask the person running the project to check the app configuration. |
| A connection error | Check your internet connection, then use the retry button on that screen. |
| **No houses yet.** or **No houses available.** | Ask your property manager to check your house access. It may not have started yet, or it may have expired. |
| **House not found** or **This house is no longer available.** | Return to Home and open the house list again. Ask your property manager if the house is still missing. |
| A message saying QR access isn't configured | Contact your property manager so the project administrator can check the access service. |
| A QR code appears but no scan is confirmed | Check the selected house, refresh the code, and try again. If it still fails, ask for the reader's connection and setup to be checked. |

When reporting a problem, include the house name, the message shown, and roughly when it happened. Keep passwords and QR codes out of your report.
