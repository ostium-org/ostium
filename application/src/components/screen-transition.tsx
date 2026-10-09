import { useEffect, useRef, type PropsWithChildren } from 'react';
import { Animated, StyleSheet } from 'react-native';
import { useIsFocused } from 'expo-router';

export function ScreenTransition({ children }: PropsWithChildren) {
  const isFocused = useIsFocused();
  const progress = useRef(new Animated.Value(isFocused ? 1 : 0)).current;

  useEffect(() => {
    const animation = Animated.timing(progress, {
      toValue: isFocused ? 1 : 0,
      duration: 220,
      useNativeDriver: true,
    });

    animation.start();
    return () => animation.stop();
  }, [isFocused, progress]);

  return (
    <Animated.View
      pointerEvents={isFocused ? 'auto' : 'none'}
      style={[
        styles.container,
        {
          opacity: progress,
          transform: [{ translateY: progress.interpolate({ inputRange: [0, 1], outputRange: [8, 0] }) }],
        },
      ]}>
      {children}
    </Animated.View>
  );
}

const styles = StyleSheet.create({
  container: {
    flex: 1,
  },
});
